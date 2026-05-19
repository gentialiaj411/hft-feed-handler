#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "mf/bench/run_metadata.hpp"
#include "mf/core/types.hpp"
#include "mf/journal/journal_reader.hpp"
#include "mf/runtime/sharded_pipeline.hpp"

#if defined(__linux__)
#include <sched.h>
#endif

namespace {

struct Config {
  std::string journal_path{};
  std::string out_path{};
  std::size_t max_events{120000};
  std::array<std::size_t, 4> shard_counts{1, 2, 4, 8};
};

std::string arg_value(int argc, char** argv, const std::string& key, const std::string& dflt) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] == key) {
      return argv[i + 1];
    }
  }
  return dflt;
}

std::size_t arg_u64(int argc, char** argv, const std::string& key, std::size_t dflt) {
  return static_cast<std::size_t>(std::stoull(arg_value(argc, argv, key, std::to_string(dflt))));
}

Config parse_args(int argc, char** argv) {
  Config cfg{};
  cfg.journal_path = arg_value(argc, argv, "--journal", "bench/results/research_2m.journal");
  cfg.out_path = arg_value(argc, argv, "--out", "bench/results/sharded_replay_wsl2_unknown.json");
  cfg.max_events = arg_u64(argc, argv, "--max-events", 120000);
  return cfg;
}

std::vector<mf::core::BookEvent> load_events(const std::string& journal_path, std::size_t max_events) {
  mf::journal::JournalReader reader;
  if (!reader.open(journal_path)) {
    throw std::runtime_error("failed to open journal: " + journal_path);
  }

  std::vector<mf::core::BookEvent> events;
  events.reserve(max_events);
  mf::core::BookEvent ev{};
  std::uint64_t ingest_ts_ns = 0;
  std::uint64_t mono_seq = 0;
  while (events.size() < max_events && reader.next(ev, ingest_ts_ns, mono_seq)) {
    events.push_back(ev);
  }
  return events;
}

std::uint64_t percentile(std::vector<std::uint64_t> values, double p) {
  if (values.empty()) {
    return 0;
  }
  const std::size_t idx = static_cast<std::size_t>(p * static_cast<double>(values.size() - 1U));
  std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(idx), values.end());
  return values[idx];
}

int current_cpu() noexcept {
#if defined(__linux__)
  return ::sched_getcpu();
#else
  return -1;
#endif
}

struct RunResult {
  std::size_t shards{1};
  double events_per_sec{0.0};
  double producer_dispatch_events_per_sec{0.0};
  double merger_drain_events_per_sec{0.0};
  double reaggregator_wall_fraction{0.0};
  std::uint64_t total_wall_ns{0};
  std::uint64_t producer_dispatch_ns{0};
  std::uint64_t reaggregator_push_ns{0};
  std::uint64_t reaggregator_drain_ns{0};
  bool reaggregator_sorted_input_fast_path{false};
  std::uint64_t p50_latency_ns{0};
  std::uint64_t p99_latency_ns{0};
  std::size_t shard_ring_capacity{0};
  std::vector<std::uint64_t> per_shard_counts{};
  std::vector<std::uint64_t> per_shard_push_retries{};
  std::vector<std::uint64_t> per_shard_worker_wall_ns{};
  std::vector<double> per_shard_worker_events_per_sec{};
  std::vector<int> per_shard_start_cpu{};
  std::vector<std::uintptr_t> per_shard_ring_address_mod64{};
  int producer_start_cpu{-1};
  int producer_end_cpu{-1};
  std::uint32_t crc{0};
};

RunResult run_once(const std::vector<mf::core::BookEvent>& events, std::size_t shards) {
  mf::runtime::ShardedPipeline<> pipeline({shards, 256, 8192});

  const auto t0 = std::chrono::steady_clock::now();
  const auto producer_t0 = std::chrono::steady_clock::now();
  const int producer_start_cpu = current_cpu();
  for (const auto& ev : events) {
    while (!pipeline.submit(ev)) {
      std::this_thread::yield();
    }
  }
  const int producer_end_cpu = current_cpu();
  const auto producer_t1 = std::chrono::steady_clock::now();
  pipeline.close_input();
  pipeline.finalize();
  const auto t1 = std::chrono::steady_clock::now();

  const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
  const double elapsed_sec = static_cast<double>(elapsed_ns) / 1e9;
  const auto producer_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(producer_t1 - producer_t0).count();
  const double producer_sec = static_cast<double>(producer_ns) / 1e9;

  const auto& stats = pipeline.stats();
  RunResult r{};
  r.shards = shards;
  r.events_per_sec = elapsed_sec > 0.0 ? static_cast<double>(events.size()) / elapsed_sec : 0.0;
  r.producer_dispatch_events_per_sec = producer_sec > 0.0 ? static_cast<double>(events.size()) / producer_sec : 0.0;
  r.merger_drain_events_per_sec = stats.reaggregator_drain_ns > 0
                                      ? static_cast<double>(events.size()) / (static_cast<double>(stats.reaggregator_drain_ns) / 1e9)
                                      : 0.0;
  r.reaggregator_wall_fraction =
      elapsed_ns > 0 ? static_cast<double>(stats.reaggregator_push_ns + stats.reaggregator_drain_ns) / static_cast<double>(elapsed_ns) : 0.0;
  r.total_wall_ns = static_cast<std::uint64_t>(elapsed_ns);
  r.producer_dispatch_ns = static_cast<std::uint64_t>(producer_ns);
  r.reaggregator_push_ns = stats.reaggregator_push_ns;
  r.reaggregator_drain_ns = stats.reaggregator_drain_ns;
  r.reaggregator_sorted_input_fast_path = stats.reaggregator_sorted_input_fast_path;
  r.p50_latency_ns = percentile(stats.publish_latency_ns, 0.50);
  r.p99_latency_ns = percentile(stats.publish_latency_ns, 0.99);
  r.shard_ring_capacity = stats.shard_ring_capacity;
  r.per_shard_counts = stats.per_shard_events;
  r.per_shard_push_retries = stats.per_shard_push_retries;
  r.per_shard_worker_wall_ns = stats.per_shard_worker_wall_ns;
  r.per_shard_start_cpu = stats.per_shard_start_cpu;
  r.per_shard_ring_address_mod64 = stats.per_shard_ring_address_mod64;
  r.producer_start_cpu = producer_start_cpu;
  r.producer_end_cpu = producer_end_cpu;
  r.per_shard_worker_events_per_sec.reserve(stats.per_shard_events.size());
  for (std::size_t i = 0; i < stats.per_shard_events.size(); ++i) {
    const double sec = static_cast<double>(stats.per_shard_worker_wall_ns[i]) / 1e9;
    r.per_shard_worker_events_per_sec.push_back(sec > 0.0 ? static_cast<double>(stats.per_shard_events[i]) / sec : 0.0);
  }
  r.crc = stats.reaggregated_crc;
  return r;
}

template <typename T>
void write_json_array(std::ofstream& out, const std::vector<T>& values) {
  out << "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i > 0) out << ",";
    out << values[i];
  }
  out << "]";
}

}  // namespace

int main(int argc, char** argv) {
#if !defined(__linux__)
  std::cout << "sharded_replay_bench is Linux/WSL2-only because journal reader is Linux-only\n";
  return 0;
#else
  try {
    const Config cfg = parse_args(argc, argv);
    const auto events = load_events(cfg.journal_path, cfg.max_events);
    if (events.size() < 100000U) {
      std::cerr << "expected at least 100000 events; got " << events.size() << "\n";
      return 2;
    }

    std::vector<RunResult> runs;
    runs.reserve(cfg.shard_counts.size());
    for (const auto shards : cfg.shard_counts) {
      runs.push_back(run_once(events, shards));
    }

    const double baseline_eps = runs.front().events_per_sec;
    const double wsl2_jitter_floor = 0.90;
    bool non_degraded = true;
    for (std::size_t i = 1; i < runs.size(); ++i) {
      if (runs[i].events_per_sec + 1e-9 < (baseline_eps * wsl2_jitter_floor)) {
        non_degraded = false;
      }
    }

    const std::uint32_t baseline_crc = runs.front().crc;
    bool crc_match = true;
    for (const auto& r : runs) {
      if (r.crc != baseline_crc) {
        crc_match = false;
      }
    }

    auto meta = mf::bench::capture_run_metadata(argc, argv);
    std::filesystem::create_directories(std::filesystem::path(cfg.out_path).parent_path());
    std::ofstream out(cfg.out_path, std::ios::trunc);

    out << "{\n";
    out << "  \"runtime_environment\": \"WSL2\",\n";
    out << "  \"journal_path\": \"" << cfg.journal_path << "\",\n";
    out << "  \"events_loaded\": " << events.size() << ",\n";
    out << "  \"metadata\": " << mf::bench::run_metadata_to_json(meta) << ",\n";
    out << "  \"crc_match_all_shards\": " << (crc_match ? "true" : "false") << ",\n";
    out << "  \"non_degraded_throughput_2plus\": " << (non_degraded ? "true" : "false") << ",\n";
    out << "  \"runs\": [\n";

    for (std::size_t i = 0; i < runs.size(); ++i) {
      const auto& r = runs[i];
      out << "    {\"shards\":" << r.shards
          << ",\"events_per_sec\":" << std::fixed << std::setprecision(2) << r.events_per_sec
          << ",\"producer_dispatch_events_per_sec\":" << r.producer_dispatch_events_per_sec
          << ",\"merger_drain_events_per_sec\":" << r.merger_drain_events_per_sec
          << ",\"reaggregator_wall_fraction\":" << std::setprecision(6) << r.reaggregator_wall_fraction
          << ",\"total_wall_ns\":" << r.total_wall_ns
          << ",\"producer_dispatch_ns\":" << r.producer_dispatch_ns
          << ",\"reaggregator_push_ns\":" << r.reaggregator_push_ns
          << ",\"reaggregator_drain_ns\":" << r.reaggregator_drain_ns
          << ",\"reaggregator_sorted_input_fast_path\":" << (r.reaggregator_sorted_input_fast_path ? "true" : "false")
          << ",\"p50_publish_latency_ns\":" << r.p50_latency_ns
          << ",\"p99_publish_latency_ns\":" << r.p99_latency_ns
          << ",\"shard_ring_capacity\":" << r.shard_ring_capacity
          << ",\"producer_start_cpu\":" << r.producer_start_cpu
          << ",\"producer_end_cpu\":" << r.producer_end_cpu
          << ",\"reaggregated_crc\":" << r.crc
          << ",\"per_shard_event_counts\":";
      write_json_array(out, r.per_shard_counts);
      out << ",\"per_shard_worker_events_per_sec\":";
      write_json_array(out, r.per_shard_worker_events_per_sec);
      out << ",\"per_shard_push_retries\":";
      write_json_array(out, r.per_shard_push_retries);
      out << ",\"per_shard_worker_wall_ns\":";
      write_json_array(out, r.per_shard_worker_wall_ns);
      out << ",\"per_shard_start_cpu\":";
      write_json_array(out, r.per_shard_start_cpu);
      out << ",\"per_shard_ring_address_mod64\":";
      write_json_array(out, r.per_shard_ring_address_mod64);
      out << "}";
      if (i + 1 < runs.size()) out << ",";
      out << "\n";
    }
    out << "  ]\n";
    out << "}\n";

    std::cout << "wrote " << cfg.out_path << "\n";
    std::cout << "crc_match_all_shards=" << (crc_match ? "true" : "false") << "\n";
    std::cout << "non_degraded_throughput_2plus=" << (non_degraded ? "true" : "false") << "\n";
    return (crc_match && non_degraded) ? 0 : 3;
  } catch (const std::exception& ex) {
    std::cerr << "error: " << ex.what() << "\n";
    return 1;
  }
#endif
}
