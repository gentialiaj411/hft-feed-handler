#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "mf/bench/run_metadata.hpp"
#include "mf/core/types.hpp"
#include "mf/journal/journal_reader.hpp"
#include "mf/journal/journal_writer.hpp"
#include "mf/phase3/book_snapshot.hpp"

namespace {

struct Config {
  std::string journal_path{"bench/results/book_reconcile_synth.journal"};
  std::string out_path{"bench/results/book_reconcile_unknown.json"};
  std::uint64_t events{100000};
  std::uint64_t snapshot_interval{10000};
  bool generate_synthetic{false};
};

std::string arg_value(int argc, char** argv, const std::string& key, const std::string& dflt) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] == key) {
      return argv[i + 1];
    }
  }
  return dflt;
}

bool has_flag(int argc, char** argv, const std::string& key) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == key) {
      return true;
    }
  }
  return false;
}

std::uint64_t arg_u64(int argc, char** argv, const std::string& key, std::uint64_t dflt) {
  return static_cast<std::uint64_t>(std::stoull(arg_value(argc, argv, key, std::to_string(dflt))));
}

Config parse_args(int argc, char** argv) {
  Config cfg{};
  cfg.journal_path = arg_value(argc, argv, "--journal", cfg.journal_path);
  cfg.out_path = arg_value(argc, argv, "--out", cfg.out_path);
  cfg.events = arg_u64(argc, argv, "--events", cfg.events);
  cfg.snapshot_interval = arg_u64(argc, argv, "--snapshot-interval", cfg.snapshot_interval);
  cfg.generate_synthetic = has_flag(argc, argv, "--generate-synthetic");
  return cfg;
}

mf::core::SymbolKey symbol_for(std::uint64_t idx) {
  mf::core::SymbolKey s{};
  const char suffix = static_cast<char>('1' + static_cast<int>(idx % 8U));
  s.bytes = {'S', 'Y', 'M', 'B', '0', '0', '0', suffix};
  return s;
}

mf::core::BookEvent make_event(std::uint64_t i) {
  const std::uint64_t symbol_idx = i % 8U;
  const std::uint64_t cycle = i / 40U;
  const std::uint64_t step = (i / 8U) % 5U;
  const std::uint64_t base = 100000000ULL + cycle * 1000ULL + symbol_idx * 10ULL;
  const std::uint32_t price = 10000U + static_cast<std::uint32_t>((cycle + symbol_idx) % 64U);

  mf::core::BookEvent ev{};
  ev.venue = static_cast<mf::core::Venue>(symbol_idx % 3U);
  ev.sequence = i + 1U;
  ev.exchange_ts_ns = i + 1U;
  ev.ingest_ts_ns = i + 1001U;
  ev.symbol = symbol_for(symbol_idx);
  ev.raw_type = static_cast<std::uint8_t>('A' + step);

  if (step == 0U) {
    ev.type = mf::core::EventType::Add;
    ev.side = mf::core::Side::Buy;
    ev.price = price;
    ev.qty = 100;
    ev.order_id = base + 1U;
  } else if (step == 1U) {
    ev.type = mf::core::EventType::Add;
    ev.side = mf::core::Side::Sell;
    ev.price = price + 20U;
    ev.qty = 120;
    ev.order_id = base + 2U;
  } else if (step == 2U) {
    ev.type = mf::core::EventType::Execute;
    ev.side = mf::core::Side::Unknown;
    ev.qty = 40;
    ev.order_id = base + 1U;
  } else if (step == 3U) {
    ev.type = mf::core::EventType::Cancel;
    ev.side = mf::core::Side::Unknown;
    ev.qty = 30;
    ev.order_id = base + 2U;
  } else {
    ev.type = mf::core::EventType::Replace;
    ev.side = mf::core::Side::Buy;
    ev.price = price + 1U;
    ev.qty = 50;
    ev.order_id = base + 3U;
    ev.reference_order_id = base + 1U;
  }
  return ev;
}

void generate_synthetic_journal(const Config& cfg) {
  const auto parent = std::filesystem::path(cfg.journal_path).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::filesystem::remove(cfg.journal_path);
  mf::journal::JournalWriter writer(1U << 20U, false);
  if (!writer.open(cfg.journal_path)) {
    throw std::runtime_error("failed to open synthetic journal for write: " + cfg.journal_path);
  }
  for (std::uint64_t i = 0; i < cfg.events; ++i) {
    const auto ev = make_event(i);
    writer.append(ev, ev.ingest_ts_ns);
  }
  writer.close();
}

mf::phase3::BookReconcileStats run_reconcile(const Config& cfg) {
  mf::journal::JournalReader reader;
  if (!reader.open(cfg.journal_path)) {
    throw std::runtime_error("failed to open journal: " + cfg.journal_path);
  }

  mf::phase3::BookReconciler reconciler(cfg.snapshot_interval);
  mf::core::BookEvent ev{};
  std::uint64_t ingest = 0;
  std::uint64_t seq = 0;
  while (reader.next(ev, ingest, seq)) {
    reconciler.on_event(ev);
  }
  return reconciler.stats();
}

}  // namespace

int main(int argc, char** argv) {
#if !defined(__linux__)
  std::cout << "book_reconcile is Linux/WSL2-only because journal reader/writer are Linux-only\n";
  return 0;
#else
  try {
    const Config cfg = parse_args(argc, argv);
    if (cfg.generate_synthetic) {
      generate_synthetic_journal(cfg);
    }

    const auto stats = run_reconcile(cfg);
    auto metadata = mf::bench::capture_run_metadata(argc, argv);
    const auto parent = std::filesystem::path(cfg.out_path).parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent);
    }

    std::ofstream out(cfg.out_path, std::ios::trunc);
    out << "{\n";
    out << "  \"runtime_environment\": \"WSL2\",\n";
    out << "  \"journal_path\": \"" << cfg.journal_path << "\",\n";
    out << "  \"events_processed\": " << stats.events_processed << ",\n";
    out << "  \"snapshots_checked\": " << stats.snapshots_checked << ",\n";
    out << "  \"divergences\": " << stats.divergences << ",\n";
    out << "  \"snapshot_interval\": " << stats.snapshot_interval << ",\n";
    out << "  \"metadata\": " << mf::bench::run_metadata_to_json(metadata) << "\n";
    out << "}\n";

    std::cout << "wrote " << cfg.out_path << "\n";
    std::cout << "events_processed=" << stats.events_processed << "\n";
    std::cout << "snapshots_checked=" << stats.snapshots_checked << "\n";
    std::cout << "divergences=" << stats.divergences << "\n";
    return stats.divergences == 0 ? 0 : 3;
  } catch (const std::exception& ex) {
    std::cerr << "error: " << ex.what() << "\n";
    return 1;
  }
#endif
}
