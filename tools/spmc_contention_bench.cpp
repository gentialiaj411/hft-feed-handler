#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "mf/bench/histogram.hpp"
#include "mf/bench/run_metadata.hpp"
#include "mf/bench/tsc_clock.hpp"
#include "mf/core/spmc_seqlock_ring.hpp"
#include "mf/phase3/types.hpp"

namespace {

struct Config {
  std::uint64_t events{500'000};
  int readers{4};
  int slow_reader{-1};
  std::uint32_t slow_every{256};
  std::uint32_t slow_sleep_ns{1'000};
  std::string out_dir{"bench/results"};
};

struct alignas(64) PaddedCursor {
  mf::core::SpmcReaderCursor cursor{};
  std::uint64_t reads{0};
  std::uint64_t overruns{0};
  std::uint64_t retry_limits{0};
  std::uint64_t torn_retries{0};
  std::uint64_t invalid_reads{0};
  char pad[64]{};
};

Config parse_args(int argc, char** argv) {
  Config cfg{};
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--events" && i + 1 < argc) cfg.events = std::stoull(argv[++i]);
    else if (arg == "--readers" && i + 1 < argc) cfg.readers = std::stoi(argv[++i]);
    else if (arg == "--slow-reader" && i + 1 < argc) cfg.slow_reader = std::stoi(argv[++i]);
    else if (arg == "--slow-every" && i + 1 < argc) cfg.slow_every = static_cast<std::uint32_t>(std::stoul(argv[++i]));
    else if (arg == "--slow-sleep-ns" && i + 1 < argc) cfg.slow_sleep_ns = static_cast<std::uint32_t>(std::stoul(argv[++i]));
    else if (arg == "--out-dir" && i + 1 < argc) cfg.out_dir = argv[++i];
  }
  if (cfg.readers < 1) cfg.readers = 1;
  return cfg;
}

std::string stamp() {
  const auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &tt);
#else
  gmtime_r(&tt, &tm);
#endif
  char out[64]{};
  std::strftime(out, sizeof(out), "%Y%m%dT%H%M%SZ", &tm);
  return out;
}

mf::phase3::FeatureVector make_feature(std::uint64_t i) {
  mf::phase3::FeatureVector fv{};
  fv.symbol_u64 = 0x4141504c20202020ULL;
  fv.exchange_ts_ns = i + 1;
  fv.ingest_ts_ns = i + 100;
  fv.nbbo_bid_price = 10'000U + static_cast<std::uint32_t>(i & 31U);
  fv.nbbo_bid_qty = 100;
  fv.nbbo_ask_price = fv.nbbo_bid_price + 10;
  fv.nbbo_ask_qty = 100;
  fv.microprice = static_cast<double>(fv.nbbo_bid_price + fv.nbbo_ask_price) * 0.5;
  return fv;
}

}  // namespace

int main(int argc, char** argv) {
  const auto cfg = parse_args(argc, argv);
  const double ticks_per_ns = mf::bench::calibrate_ticks_per_ns();
  auto ring = std::make_unique<mf::core::SPMCSeqlockRing<mf::phase3::FeatureVector, 1U << 18U>>();
  std::vector<PaddedCursor> cursors(static_cast<std::size_t>(cfg.readers));
  std::atomic<bool> start{false};
  std::atomic<bool> done{false};
  mf::bench::LatencyHistogram publish_hist{1, 10'000'000, 3};
  std::vector<std::thread> threads;
  threads.reserve(static_cast<std::size_t>(cfg.readers));

  for (int r = 0; r < cfg.readers; ++r) {
    threads.emplace_back([&, r]() {
      auto& state = cursors[static_cast<std::size_t>(r)];
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      while (!done.load(std::memory_order_acquire) || state.cursor.next_sequence < ring->published_sequence()) {
        mf::phase3::FeatureVector out{};
        const auto result = ring->try_read_next(state.cursor, out);
        if (result.status == mf::core::SpmcReadStatus::Success) {
          ++state.reads;
          if (out.nbbo_ask_price <= out.nbbo_bid_price || out.exchange_ts_ns == 0) {
            ++state.invalid_reads;
          }
          if (r == cfg.slow_reader && cfg.slow_every > 0 && (state.reads % cfg.slow_every) == 0) {
            std::this_thread::sleep_for(std::chrono::nanoseconds(cfg.slow_sleep_ns));
          }
        } else if (result.status == mf::core::SpmcReadStatus::Overrun) {
          state.overruns += result.overruns;
        } else if (result.status == mf::core::SpmcReadStatus::RetryLimit) {
          ++state.retry_limits;
        } else {
          std::this_thread::yield();
        }
        state.torn_retries += result.torn_read_retries;
      }
    });
  }

  start.store(true, std::memory_order_release);
  const auto total_begin = mf::bench::tsc_now();
  for (std::uint64_t i = 0; i < cfg.events; ++i) {
    const auto t0 = mf::bench::tsc_now();
    (void)ring->try_publish(make_feature(i));
    const auto t1 = mf::bench::tsc_now();
    publish_hist.record(mf::bench::ticks_to_ns(t1 - t0, ticks_per_ns));
  }
  const auto total_end = mf::bench::tsc_now();
  done.store(true, std::memory_order_release);
  for (auto& t : threads) t.join();

  std::filesystem::create_directories(cfg.out_dir);
  const auto path = cfg.out_dir + "/spmc_contention_" + stamp() + ".md";
  auto meta = mf::bench::capture_run_metadata(argc, argv);
  const auto total_ns = mf::bench::ticks_to_ns(total_end - total_begin, ticks_per_ns);
  const double publish_eps = total_ns > 0 ? static_cast<double>(cfg.events) * 1'000'000'000.0 / static_cast<double>(total_ns) : 0.0;

  std::ofstream os(path);
  os << "# SPMC Contention Report\n\n";
  os << "- events: " << cfg.events << "\n";
  os << "- readers: " << cfg.readers << "\n";
  os << "- slow_reader: " << cfg.slow_reader << "\n";
  os << "- slow_every: " << cfg.slow_every << "\n";
  os << "- slow_sleep_ns: " << cfg.slow_sleep_ns << "\n";
  os << "- cursor_alignment: " << alignof(PaddedCursor) << "\n";
  os << "- cursor_size: " << sizeof(PaddedCursor) << "\n";
  os << "- writer_events_per_sec: " << publish_eps << "\n\n";
  os << "| publish p50_ns | p99_ns | p99.9_ns | p99.99_ns | max_ns |\n";
  os << "|---:|---:|---:|---:|---:|\n";
  os << "| " << publish_hist.percentile(0.50)
     << " | " << publish_hist.percentile(0.99)
     << " | " << publish_hist.percentile(0.999)
     << " | " << publish_hist.percentile(0.9999)
     << " | " << publish_hist.max()
     << " |\n\n";
  os << "| reader | reads | overruns | retry_limits | torn_retries | invalid_reads | final_lag |\n";
  os << "|---:|---:|---:|---:|---:|---:|---:|\n";
  for (int r = 0; r < cfg.readers; ++r) {
    const auto& state = cursors[static_cast<std::size_t>(r)];
    const auto published = ring->published_sequence();
    const auto lag = published > state.cursor.next_sequence ? published - state.cursor.next_sequence : 0;
    os << "| " << r
       << " | " << state.reads
       << " | " << state.overruns
       << " | " << state.retry_limits
       << " | " << state.torn_retries
       << " | " << state.invalid_reads
       << " | " << lag
       << " |\n";
  }
  os << "\nMethodology: one writer, cache-line-padded per-reader cursors, optional adversarial slow consumer, RDTSCP publish timing where available.\n";
  os << "Metadata: " << mf::bench::run_metadata_to_json(meta) << "\n";
  std::cout << "spmc_contention_report=" << path << "\n";
  return 0;
}
