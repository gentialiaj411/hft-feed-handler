#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "mf/bench/run_metadata.hpp"
#include "mf/core/spmc_seqlock_ring.hpp"
#include "mf/phase3/types.hpp"

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace {

struct Config {
  std::size_t events{500000};
  std::string out_path{"bench/results/spmc_fanout_wsl2_unknown.json"};
  std::array<int, 4> reader_counts{1, 2, 4, 8};
};

std::string arg_value(int argc, char** argv, const std::string& key, const std::string& dflt) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] == key) {
      return argv[i + 1];
    }
  }
  return dflt;
}

std::size_t arg_size(int argc, char** argv, const std::string& key, std::size_t dflt) {
  return static_cast<std::size_t>(std::stoull(arg_value(argc, argv, key, std::to_string(dflt))));
}

Config parse_args(int argc, char** argv) {
  Config cfg{};
  cfg.events = arg_size(argc, argv, "--events", cfg.events);
  cfg.out_path = arg_value(argc, argv, "--out", cfg.out_path);
  return cfg;
}

int current_cpu() noexcept {
#if defined(__linux__)
  return ::sched_getcpu();
#else
  return -1;
#endif
}

void pin_current_thread_to_cpu(int cpu) noexcept {
#if defined(__linux__)
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  (void)::pthread_setaffinity_np(::pthread_self(), sizeof(set), &set);
#else
  (void)cpu;
#endif
}

std::uint64_t percentile(std::vector<std::uint64_t> values, double p) {
  if (values.empty()) {
    return 0;
  }
  const std::size_t idx = static_cast<std::size_t>(p * static_cast<double>(values.size() - 1U));
  std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(idx), values.end());
  return values[idx];
}

mf::phase3::FeatureVector make_feature(std::uint64_t i) {
  mf::phase3::FeatureVector fv{};
  fv.symbol_u64 = 0x4141504c20202020ULL;
  fv.exchange_ts_ns = i + 1U;
  fv.ingest_ts_ns = i + 1000U;
  fv.nbbo_bid_price = 10000U + static_cast<std::uint32_t>(i & 31U);
  fv.nbbo_bid_qty = 100U + static_cast<std::uint32_t>(i & 15U);
  fv.nbbo_ask_price = fv.nbbo_bid_price + 10U;
  fv.nbbo_ask_qty = fv.nbbo_bid_qty + 1U;
  fv.microprice = static_cast<double>(fv.nbbo_bid_price + fv.nbbo_ask_price) * 0.5;
  fv.ofi = static_cast<double>(static_cast<int>(i & 7U) - 3);
  fv.queue_ahead = static_cast<double>(i & 255U);
  fv.effective_spread = 1.0;
  fv.kyle_lambda = 0.01;
  fv.vpin = 0.02;
  return fv;
}

struct ReaderResult {
  std::uint64_t reads{0};
  std::uint64_t torn_retries{0};
  std::uint64_t overruns{0};
  std::uint64_t retry_limits{0};
  std::uint64_t invalid_reads{0};
  std::uint64_t p50_read_latency_ns{0};
  std::uint64_t p99_read_latency_ns{0};
  int start_cpu{-1};
  int end_cpu{-1};
};

struct RunResult {
  int readers{0};
  double writer_publish_events_per_sec{0.0};
  std::uint64_t writer_publish_ns{0};
  int writer_start_cpu{-1};
  int writer_end_cpu{-1};
  std::vector<ReaderResult> reader_results{};
};

RunResult run_once(std::size_t events, int reader_count) {
  auto ring = std::make_unique<mf::core::SPMCSeqlockRing<mf::phase3::FeatureVector, 1U << 20U>>();
  std::atomic<bool> start{false};
  std::atomic<bool> writer_done{false};
  std::vector<ReaderResult> reader_results(static_cast<std::size_t>(reader_count));
  std::vector<std::thread> readers;
  readers.reserve(static_cast<std::size_t>(reader_count));

  for (int r = 0; r < reader_count; ++r) {
    readers.emplace_back([&, r]() {
      auto& rr = reader_results[static_cast<std::size_t>(r)];
      pin_current_thread_to_cpu(r + 1);
      rr.start_cpu = current_cpu();
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      mf::core::SpmcReaderCursor cursor{};
      std::vector<std::uint64_t> samples;
      samples.reserve(events);
      while (!writer_done.load(std::memory_order_acquire) || cursor.next_sequence < ring->published_sequence()) {
        mf::phase3::FeatureVector out{};
        const auto t0 = std::chrono::steady_clock::now();
        const auto result = ring->try_read_next(cursor, out);
        const auto t1 = std::chrono::steady_clock::now();
        if (result.status == mf::core::SpmcReadStatus::Success) {
          samples.push_back(static_cast<std::uint64_t>(
              std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
          ++rr.reads;
          if (out.exchange_ts_ns == 0 || out.nbbo_ask_price <= out.nbbo_bid_price) {
            ++rr.invalid_reads;
          }
        } else if (result.status == mf::core::SpmcReadStatus::Overrun) {
          rr.overruns += result.overruns;
        } else if (result.status == mf::core::SpmcReadStatus::RetryLimit) {
          ++rr.retry_limits;
        } else {
          std::this_thread::yield();
        }
        rr.torn_retries += result.torn_read_retries;
      }
      rr.p50_read_latency_ns = percentile(samples, 0.50);
      rr.p99_read_latency_ns = percentile(samples, 0.99);
      rr.end_cpu = current_cpu();
    });
  }

  RunResult run{};
  run.readers = reader_count;
  std::thread writer([&]() {
    pin_current_thread_to_cpu(0);
    run.writer_start_cpu = current_cpu();
    start.store(true, std::memory_order_release);
    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < events; ++i) {
      (void)ring->try_publish(make_feature(static_cast<std::uint64_t>(i)));
    }
    const auto t1 = std::chrono::steady_clock::now();
    run.writer_end_cpu = current_cpu();
    run.writer_publish_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    writer_done.store(true, std::memory_order_release);
  });

  writer.join();
  for (auto& reader : readers) {
    reader.join();
  }
  run.writer_publish_events_per_sec =
      run.writer_publish_ns > 0 ? static_cast<double>(events) / (static_cast<double>(run.writer_publish_ns) / 1e9) : 0.0;
  run.reader_results = std::move(reader_results);
  return run;
}

}  // namespace

int main(int argc, char** argv) {
  const Config cfg = parse_args(argc, argv);
  std::vector<RunResult> runs;
  runs.reserve(cfg.reader_counts.size());
  for (const int readers : cfg.reader_counts) {
    runs.push_back(run_once(cfg.events, readers));
  }

  const double one_reader_rate = runs.empty() ? 0.0 : runs.front().writer_publish_events_per_sec;
  bool writer_non_degraded_1_to_4 = false;
  for (const auto& run : runs) {
    if (run.readers == 4) {
      writer_non_degraded_1_to_4 = run.writer_publish_events_per_sec >= (one_reader_rate * 0.90);
    }
  }

  auto metadata = mf::bench::capture_run_metadata(argc, argv);
  const auto parent = std::filesystem::path(cfg.out_path).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream out(cfg.out_path, std::ios::trunc);
  out << "{\n";
  out << "  \"runtime_environment\": \"WSL2\",\n";
  out << "  \"events\": " << cfg.events << ",\n";
  out << "  \"metadata\": " << mf::bench::run_metadata_to_json(metadata) << ",\n";
  out << "  \"writer_non_degraded_1_to_4_readers\": " << (writer_non_degraded_1_to_4 ? "true" : "false") << ",\n";
  out << "  \"runs\": [\n";
  for (std::size_t i = 0; i < runs.size(); ++i) {
    const auto& run = runs[i];
    out << "    {\"readers\":" << run.readers
        << ",\"writer_publish_events_per_sec\":" << std::fixed << std::setprecision(2) << run.writer_publish_events_per_sec
        << ",\"writer_publish_ns\":" << run.writer_publish_ns
        << ",\"writer_start_cpu\":" << run.writer_start_cpu
        << ",\"writer_end_cpu\":" << run.writer_end_cpu
        << ",\"reader_results\":[";
    for (std::size_t r = 0; r < run.reader_results.size(); ++r) {
      const auto& rr = run.reader_results[r];
      out << "{\"reads\":" << rr.reads
          << ",\"p50_read_latency_ns\":" << rr.p50_read_latency_ns
          << ",\"p99_read_latency_ns\":" << rr.p99_read_latency_ns
          << ",\"torn_read_retries\":" << rr.torn_retries
          << ",\"overruns\":" << rr.overruns
          << ",\"retry_limits\":" << rr.retry_limits
          << ",\"invalid_reads\":" << rr.invalid_reads
          << ",\"start_cpu\":" << rr.start_cpu
          << ",\"end_cpu\":" << rr.end_cpu
          << "}";
      if (r + 1 < run.reader_results.size()) out << ",";
    }
    out << "]}";
    if (i + 1 < runs.size()) out << ",";
    out << "\n";
  }
  out << "  ]\n";
  out << "}\n";

  std::cout << "wrote " << cfg.out_path << "\n";
  std::cout << "writer_non_degraded_1_to_4_readers=" << (writer_non_degraded_1_to_4 ? "true" : "false") << "\n";
  return writer_non_degraded_1_to_4 ? 0 : 3;
}
