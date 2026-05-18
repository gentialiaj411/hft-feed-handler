#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>

#include "mf/bench/histogram.hpp"
#include "mf/bench/run_metadata.hpp"
#include "mf/core/spsc_ring.hpp"
#include "mf/core/spsc_ring_aligned_storage.hpp"
#include "mf/core/time.hpp"
#include "mf/core/types.hpp"
#include "mf/phase2/sequence_tracker.hpp"
#include "mf/phase3/feature_bridge.hpp"
#include "mf/phase4/backtest_runner.hpp"

namespace {

struct ThroughputStats {
  double mean{0.0};
  double median{0.0};
  double min{0.0};
  double max{0.0};
};

struct BenchOut {
  std::string bench_id{};
  bool skipped{false};
  std::string reason{};
  mf::bench::LatencyHistogram hist{};
  ThroughputStats tps{};
  std::uint64_t events{0};
};

std::string arg(int argc, char** argv, const std::string& k, const std::string& d) {
  for (int i = 1; i + 1 < argc; ++i) if (argv[i] == k) return argv[i + 1];
  return d;
}

std::uint64_t arg_u64(int argc, char** argv, const std::string& k, std::uint64_t d) {
  return static_cast<std::uint64_t>(std::stoull(arg(argc, argv, k, std::to_string(d))));
}

std::string utc_stamp() {
  const auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &tt);
#else
  gmtime_r(&tt, &tm);
#endif
  char out[64];
  std::strftime(out, sizeof(out), "%Y%m%dT%H%M%SZ", &tm);
  return out;
}

ThroughputStats summarize(std::vector<double> xs) {
  ThroughputStats s{};
  if (xs.empty()) return s;
  s.min = *std::min_element(xs.begin(), xs.end());
  s.max = *std::max_element(xs.begin(), xs.end());
  s.mean = std::accumulate(xs.begin(), xs.end(), 0.0) / static_cast<double>(xs.size());
  std::sort(xs.begin(), xs.end());
  s.median = xs[xs.size() / 2U];
  return s;
}

mf::core::BookEvent make_event(std::uint64_t i) {
  mf::core::BookEvent ev{};
  ev.venue = static_cast<mf::core::Venue>(i % 3U);
  ev.type = mf::core::EventType::Add;
  ev.sequence = i + 1;
  ev.exchange_ts_ns = i + 1;
  ev.ingest_ts_ns = mf::core::monotonic_raw_now_ns();
  ev.order_id = i + 11;
  ev.qty = 100;
  ev.price = 10000 + static_cast<std::uint32_t>(i & 63U);
  ev.side = (i & 1U) ? mf::core::Side::Buy : mf::core::Side::Sell;
  ev.symbol.bytes = {'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '};
  return ev;
}

template <typename Fn>
BenchOut run_hist_bench(const std::string& id, std::uint64_t events, int warmups, int reps, Fn fn) {
  BenchOut out{};
  out.bench_id = id;
  out.events = events;
  std::vector<double> rates;
  for (int i = 0; i < warmups + reps; ++i) {
    mf::bench::LatencyHistogram h{};
    const auto t0 = std::chrono::steady_clock::now();
    fn(events, h);
    const auto t1 = std::chrono::steady_clock::now();
    const double sec = std::chrono::duration<double>(t1 - t0).count();
    const double rate = (sec > 0.0) ? static_cast<double>(events) / sec : 0.0;
    if (i >= warmups) {
      out.hist.merge(h);
      rates.push_back(rate);
    }
  }
  out.tps = summarize(rates);
  return out;
}

std::string write_bench_json(const BenchOut& out, const mf::bench::RunMetadata& meta, int warmups, int reps) {
  std::filesystem::create_directories("bench/results");
  const std::string path = "bench/results/sweep_" + out.bench_id + "_" + utc_stamp() + ".json";
  std::ofstream os(path);
  os << "{"
     << "\"bench_id\":\"" << out.bench_id << "\","
     << "\"metadata\":" << mf::bench::run_metadata_to_json(meta) << ","
     << "\"events\":" << out.events << ","
     << "\"warmup_reps\":" << warmups << ","
     << "\"measured_reps\":" << reps << ","
     << "\"skipped\":" << (out.skipped ? "true" : "false") << ","
     << "\"skip_reason\":\"" << out.reason << "\","
     << "\"throughput_mps\":{\"mean\":" << out.tps.mean << ",\"median\":" << out.tps.median << ",\"min\":" << out.tps.min << ",\"max\":" << out.tps.max << "},"
     << "\"latency\":{\"p50\":" << out.hist.percentile(0.50) << ",\"p90\":" << out.hist.percentile(0.90)
     << ",\"p99\":" << out.hist.percentile(0.99) << ",\"p999\":" << out.hist.percentile(0.999)
     << ",\"max\":" << out.hist.max() << "},"
     << "\"histogram\":" << out.hist.to_json_buckets()
     << "}\n";
  return path;
}

}  // namespace

int main(int argc, char** argv) {
  const std::uint64_t events = arg_u64(argc, argv, "--events", 5'000'000ULL);
  const int warmups = static_cast<int>(arg_u64(argc, argv, "--warmup-reps", 3));
  const int reps = static_cast<int>(arg_u64(argc, argv, "--measured-reps", 5));
  auto meta = mf::bench::capture_run_metadata(argc, argv);

  constexpr std::size_t kCap = 1U << 20U;
  std::vector<BenchOut> outs;

  outs.push_back(run_hist_bench("b1_feed_hot_path", events, warmups, reps, [](std::uint64_t n, mf::bench::LatencyHistogram& h) {
    mf::core::SPSCRingBuffer<mf::core::BookEvent, kCap> ring;
    mf::phase2::MultiVenueSequenceTracker seq(256);
    mf::core::BookEvent tmp{};
    for (std::uint64_t i = 0; i < n; ++i) {
      auto ev = make_event(i);
      const auto t0 = mf::core::monotonic_raw_now_ns();
      const auto st = seq.on_sequence(ev.venue, ev.sequence);
      if (st.status == mf::phase2::SequenceStatus::InOrder) (void)ring.try_push(ev);
      while (ring.try_pop(tmp)) {}
      h.record(mf::core::monotonic_raw_now_ns() - t0);
    }
  }));

  outs.push_back(run_hist_bench("b2_feature_latency", events, warmups, reps, [](std::uint64_t n, mf::bench::LatencyHistogram& h) {
    mf::core::SPSCRingBuffer<mf::phase3::FeatureVector, kCap> ring;
    mf::phase3::RingFeaturePublisher<kCap> pub(&ring);
    mf::phase3::FeatureBridge bridge(&pub);
    mf::phase3::FeatureVector fv{};
    for (std::uint64_t i = 0; i < n; ++i) {
      auto ev = make_event(i);
      const auto t0 = mf::core::monotonic_raw_now_ns();
      bridge.on_merged_event(ev);
      while (ring.try_pop(fv)) {}
      h.record(mf::core::monotonic_raw_now_ns() - t0);
    }
  }));

  outs.push_back(run_hist_bench("b3_tuned_pin_huge", events, warmups, reps, [](std::uint64_t n, mf::bench::LatencyHistogram& h) {
    mf::core::SpscBackingInfo info{};
    auto ring_ptr = mf::core::make_spsc_ring_on_node<mf::core::BookEvent, kCap>(0, true, info);
    auto* ring = ring_ptr.get();
    mf::core::BookEvent tmp{};
    for (std::uint64_t i = 0; i < n; ++i) {
      auto ev = make_event(i);
      const auto t0 = mf::core::monotonic_raw_now_ns();
      while (!ring->try_push(ev)) {}
      while (ring->try_pop(tmp)) {}
      h.record(mf::core::monotonic_raw_now_ns() - t0);
    }
  }));

  BenchOut b4{};
  b4.bench_id = "b4_wire_loopback";
  b4.events = events;
  b4.skipped = true;
  b4.reason = "TODO/VERIFY: requires Linux multicast runtime in this environment";
  outs.push_back(b4);

  outs.push_back(run_hist_bench("b5_backtest_throughput", events, warmups, reps, [](std::uint64_t n, mf::bench::LatencyHistogram& h) {
    std::vector<mf::core::BookEvent> tape;
    tape.reserve(static_cast<std::size_t>(n));
    for (std::uint64_t i = 0; i < n; ++i) tape.push_back(make_event(i));
    mf::phase4::BacktestRunner runner;
    const auto t0 = mf::core::monotonic_raw_now_ns();
    (void)runner.run(tape);
    const auto elapsed = mf::core::monotonic_raw_now_ns() - t0;
    const auto per_ev = (n > 0) ? (elapsed / n) : 0;
    for (std::uint64_t i = 0; i < n; ++i) h.record(per_ev);
  }));

  std::vector<std::string> paths;
  for (const auto& o : outs) paths.push_back(write_bench_json(o, meta, warmups, reps));

  const std::string summary = "bench/results/sweep_summary_" + utc_stamp() + ".json";
  std::ofstream os(summary);
  os << "{\"metadata\":" << mf::bench::run_metadata_to_json(meta) << ",\"artifacts\":[";
  for (std::size_t i = 0; i < paths.size(); ++i) {
    if (i > 0) os << ",";
    os << "\"" << paths[i] << "\"";
  }
  os << "]}\n";

  std::printf("%-22s %-12s %-12s %-10s %-10s\n", "bench", "throughput", "p99(ns)", "p99.9", "max");
  for (const auto& o : outs) {
    if (o.skipped) {
      std::printf("%-22s %s\n", o.bench_id.c_str(), "SKIP");
      continue;
    }
    std::printf("%-22s %-12.2f %-12llu %-10llu %-10llu\n",
                o.bench_id.c_str(),
                o.tps.mean,
                static_cast<unsigned long long>(o.hist.percentile(0.99)),
                static_cast<unsigned long long>(o.hist.percentile(0.999)),
                static_cast<unsigned long long>(o.hist.max()));
  }
  std::printf("summary=%s\n", summary.c_str());
  return 0;
}
