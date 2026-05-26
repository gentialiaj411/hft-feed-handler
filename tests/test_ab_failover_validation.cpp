#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "mf/core/types.hpp"
#include "mf/journal/journal_writer.hpp"

namespace {

mf::core::BookEvent make_ev(std::uint64_t seq) {
  mf::core::BookEvent ev{};
  ev.venue = mf::core::Venue::Nasdaq;
  ev.type = mf::core::EventType::Add;
  ev.sequence = seq;
  ev.exchange_ts_ns = seq * 1000ULL;
  ev.ingest_ts_ns = ev.exchange_ts_ns + 1ULL;
  ev.side = ((seq & 1ULL) == 0ULL) ? mf::core::Side::Buy : mf::core::Side::Sell;
  ev.price = static_cast<std::uint32_t>(10000ULL + seq);
  ev.qty = 100U;
  ev.order_id = seq;
  ev.raw_type = static_cast<std::uint8_t>('A');
  ev.symbol.bytes = {'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '};
  return ev;
}

bool file_contains(const std::string& path, const std::string& needle) {
  std::ifstream in(path);
  if (!in) return false;
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return content.find(needle) != std::string::npos;
}

}  // namespace

int main() {
#if !defined(__linux__)
  return 0;
#else
  const std::filesystem::path journal_path = "ab_failover_validation_smoke.journal";
  const std::filesystem::path output_path = "ab_failover_validation_smoke.json";

  std::filesystem::remove(journal_path);
  std::filesystem::remove(output_path);

  mf::journal::JournalWriter writer(1U << 16U, false);
  assert(writer.open(journal_path.string()));
  for (std::uint64_t i = 1; i <= 256; ++i) {
    const auto ev = make_ev(i);
    writer.append(ev, ev.ingest_ts_ns);
  }
  writer.close();

  const std::string cmd =
      "./ab_failover_validation --journal " + journal_path.string() +
      " --output " + output_path.string() + " --seed 42 --drop-rate-a 0.5";
  const int rc = std::system(cmd.c_str());
  assert(rc == 0);

  assert(file_contains(output_path.string(), "\"baseline_crc\""));
  assert(file_contains(output_path.string(), "\"drop_injected_crc\""));
  assert(file_contains(output_path.string(), "\"events_processed\": 256"));
  assert(file_contains(output_path.string(), "\"crc_match\": true"));

  std::filesystem::remove(journal_path);
  std::filesystem::remove(output_path);
  return 0;
#endif
}
