#include <cstdio>
#include <algorithm>
#include <array>
#include <filesystem>
#include <string>

#include "mf/core/book_event_crc.hpp"
#include "mf/journal/journal_writer.hpp"

namespace {
std::string arg(int argc, char** argv, const std::string& key, const std::string& dflt) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] == key) return argv[i + 1];
  }
  return dflt;
}

std::uint64_t arg_u64(int argc, char** argv, const std::string& key, std::uint64_t dflt) {
  return static_cast<std::uint64_t>(std::stoull(arg(argc, argv, key, std::to_string(dflt))));
}

bool has_flag(int argc, char** argv, const std::string& key) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == key) return true;
  }
  return false;
}

mf::core::BookEvent make_event(std::uint64_t i, std::uint64_t symbol_count) {
  static constexpr std::array<std::array<char, 8>, 8> kSymbols = {{
      {'A', 'A', 'P', 'L', '0', '0', '0', '1'},
      {'M', 'S', 'F', 'T', '0', '0', '0', '2'},
      {'N', 'V', 'D', 'A', '0', '0', '0', '3'},
      {'A', 'M', 'Z', 'N', '0', '0', '0', '4'},
      {'G', 'O', 'O', 'G', '0', '0', '0', '5'},
      {'M', 'E', 'T', 'A', '0', '0', '0', '6'},
      {'T', 'S', 'L', 'A', '0', '0', '0', '7'},
      {'A', 'M', 'D', '0', '0', '0', '0', '8'},
  }};
  const std::uint64_t effective_symbol_count = (symbol_count == 0) ? 1U : std::min<std::uint64_t>(symbol_count, kSymbols.size());
  const std::uint64_t symbol_slot = i % effective_symbol_count;
  const std::uint64_t cycle = i / (effective_symbol_count * 6U);
  const std::uint64_t step = (i / effective_symbol_count) % 6U;
  const std::size_t symbol_idx = static_cast<std::size_t>(symbol_slot);
  const std::uint32_t mid = 10000U + static_cast<std::uint32_t>(cycle % 97U);
  const std::uint64_t bid_order = 1 + (cycle * 2U);
  const std::uint64_t ask_order = bid_order + 1U;

  mf::core::BookEvent ev{};
  ev.venue = mf::core::Venue::Nasdaq;
  ev.sequence = i + 1U;
  ev.exchange_ts_ns = i + 1U;
  ev.ingest_ts_ns = i + 1001U;
  ev.symbol.bytes = kSymbols[symbol_idx];
  ev.qty = 100;

  if (step == 0U) {
    ev.type = mf::core::EventType::Add;
    ev.side = mf::core::Side::Buy;
    ev.price = mid - 1U;
    ev.order_id = bid_order;
  } else if (step == 1U) {
    ev.type = mf::core::EventType::Add;
    ev.side = mf::core::Side::Sell;
    ev.price = mid + 1U;
    ev.order_id = ask_order;
  } else if (step == 2U) {
    ev.type = mf::core::EventType::Trade;
    ev.side = mf::core::Side::Sell;
    ev.price = mid - 1U;
    ev.qty = 80;
    ev.match_id = i + 10U;
  } else if (step == 3U) {
    ev.type = mf::core::EventType::Trade;
    ev.side = mf::core::Side::Buy;
    ev.price = mid + 1U;
    ev.qty = 80;
    ev.match_id = i + 10U;
  } else if (step == 4U) {
    ev.type = mf::core::EventType::Delete;
    ev.side = mf::core::Side::Buy;
    ev.price = mid - 1U;
    ev.order_id = bid_order;
  } else {
    ev.type = mf::core::EventType::Delete;
    ev.side = mf::core::Side::Sell;
    ev.price = mid + 1U;
    ev.order_id = ask_order;
  }

  return ev;
}
}  // namespace

int main(int argc, char** argv) {
#if !defined(__linux__)
  std::printf("make_research_journal is Linux-only because journal writer uses Linux append path\n");
  return 0;
#else
  const std::string out = arg(argc, argv, "--out", "bench/results/research_2m.journal");
  const std::uint64_t events = arg_u64(argc, argv, "--events", 2'000'000ULL);
  const std::uint64_t symbol_count = arg_u64(argc, argv, "--symbol-count", 1ULL);
  const bool fsync = has_flag(argc, argv, "--fsync");

  if (events == 0) {
    std::printf("events must be > 0\n");
    return 2;
  }

  const auto parent = std::filesystem::path(out).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }

  mf::journal::JournalWriter writer(1U << 20U, fsync);
  if (!writer.open(out)) {
    std::printf("failed to open output journal: %s\n", out.c_str());
    return 1;
  }

  std::uint32_t crc = 0;
  for (std::uint64_t i = 0; i < events; ++i) {
    const auto ev = make_event(i, symbol_count);
    mf::core::update_crc_from_book_event(crc, ev);
    writer.append(ev, ev.ingest_ts_ns);
  }
  writer.close();

  std::printf("journal=%s events=%llu symbol_count=%llu input_crc=%u fsync=%s\n",
              out.c_str(),
              static_cast<unsigned long long>(events),
              static_cast<unsigned long long>(symbol_count),
              crc,
              fsync ? "true" : "false");
  return 0;
#endif
}
