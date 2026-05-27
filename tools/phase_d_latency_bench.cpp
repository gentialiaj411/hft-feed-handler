#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <sys/resource.h>
#endif

#include "mf/core/spsc_ring.hpp"
#include "mf/core/spsc_ring_aligned_storage.hpp"
#include "mf/core/time.hpp"
#include "mf/core/types.hpp"
#include "mf/os/cpu_affinity.hpp"
#include "mf/runtime/pinning_config.hpp"

namespace {

struct Config {
  std::string mode{"both"};
  std::uint64_t events{2'000'000};
  std::string json_out{};
  mf::runtime::PinningConfig pin{};
};

struct BenchResult {
  std::string label{};
  double throughput_mps{0.0};
  std::uint64_t p50_ns{0};
  std::uint64_t p99_ns{0};
  std::uint64_t p999_ns{0};
  std::uint64_t max_ns{0};
  std::uint64_t page_faults{0};
  std::uint64_t ctx_switches{0};
  mf::core::SpscBackingInfo backing{};
};

Config parse_args(int argc, char** argv) {
  Config c{};
  c.pin = mf::runtime::pinning_config_from_cli(argc, argv);
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--config" && i + 1 < argc) c.mode = argv[++i];
    else if (a == "--events" && i + 1 < argc) c.events = static_cast<std::uint64_t>(std::stoull(argv[++i]));
    else if (a == "--json" && i + 1 < argc) c.json_out = argv[++i];
  }
  return c;
}

std::uint64_t pct(std::vector<std::uint64_t>& v, double p) {
  if (v.empty()) return 0;
  const std::size_t idx = static_cast<std::size_t>(p * static_cast<double>(v.size() - 1));
  std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(idx), v.end());
  return v[idx];
}

struct ThreadRusage {
  std::uint64_t faults{0};
  std::uint64_t csw{0};
};

ThreadRusage read_thread_usage() {
  ThreadRusage out{};
#if defined(__linux__)
  rusage ru{};
  if (::getrusage(RUSAGE_THREAD, &ru) == 0) {
    out.faults = static_cast<std::uint64_t>(ru.ru_minflt + ru.ru_majflt);
    out.csw = static_cast<std::uint64_t>(ru.ru_nvcsw + ru.ru_nivcsw);
  }
#endif
  return out;
}

template <std::size_t Capacity>
BenchResult run_one(const std::string& label, const Config& cfg, bool tuned) {
  BenchResult out{};
  out.label = label;
  constexpr std::size_t kCap = Capacity;

  std::unique_ptr<mf::core::SPSCRingBuffer<mf::core::BookEvent, kCap>> base_ring{};
  mf::core::SpscRingPtr<mf::core::BookEvent, kCap> tuned_ring{};
  mf::core::SPSCRingBuffer<mf::core::BookEvent, kCap>* ring = nullptr;
  if (!tuned) {
    base_ring = std::make_unique<mf::core::SPSCRingBuffer<mf::core::BookEvent, kCap>>();
    ring = base_ring.get();
    out.backing.kind = mf::core::SpscBackingKind::Heap;
  } else {
    tuned_ring = mf::core::make_spsc_ring_on_node<mf::core::BookEvent, kCap>(
        cfg.pin.ring_numa_node, cfg.pin.ring_hugepages, out.backing);
    ring = tuned_ring.get();
  }

  std::vector<std::uint64_t> lats;
  lats.reserve(static_cast<std::size_t>(cfg.events));
  std::atomic<std::uint64_t> consumed{0};
  std::atomic<bool> producer_done{false};
  ThreadRusage prod_before{};
  ThreadRusage prod_after{};
  ThreadRusage con_before{};
  ThreadRusage con_after{};

  const auto t0 = std::chrono::steady_clock::now();
  std::thread consumer([&]() {
    if (tuned && cfg.pin.consumer_cpu >= 0) (void)mf::os::pin_current_thread(cfg.pin.consumer_cpu);
    if (tuned && cfg.pin.realtime) (void)mf::os::set_realtime_fifo(cfg.pin.rt_priority);
    con_before = read_thread_usage();
    mf::core::BookEvent ev{};
    while (!producer_done.load(std::memory_order_acquire) || consumed.load(std::memory_order_relaxed) < cfg.events) {
      if (!ring->try_pop(ev)) continue;
      const std::uint64_t now = mf::core::monotonic_raw_now_ns();
      lats.push_back((now > ev.ingest_ts_ns) ? (now - ev.ingest_ts_ns) : 0ULL);
      consumed.fetch_add(1, std::memory_order_release);
    }
    con_after = read_thread_usage();
  });

  std::thread producer([&]() {
    if (tuned && cfg.pin.producer_cpu >= 0) (void)mf::os::pin_current_thread(cfg.pin.producer_cpu);
    if (tuned && cfg.pin.realtime) (void)mf::os::set_realtime_fifo(cfg.pin.rt_priority);
    prod_before = read_thread_usage();
    for (std::uint64_t i = 0; i < cfg.events; ++i) {
      mf::core::BookEvent ev{};
      ev.venue = mf::core::Venue::Nasdaq;
      ev.type = mf::core::EventType::Add;
      ev.sequence = i + 1;
      ev.exchange_ts_ns = i + 1;
      ev.ingest_ts_ns = mf::core::monotonic_raw_now_ns();
      while (!ring->try_push(ev)) {}
    }
    prod_after = read_thread_usage();
    producer_done.store(true, std::memory_order_release);
  });

  producer.join();
  consumer.join();
  const auto t1 = std::chrono::steady_clock::now();
  const double sec = std::chrono::duration<double>(t1 - t0).count();
  out.throughput_mps = (sec > 0.0) ? static_cast<double>(cfg.events) / sec : 0.0;

  auto p50v = lats;
  auto p99v = lats;
  auto p999v = lats;
  out.p50_ns = pct(p50v, 0.50);
  out.p99_ns = pct(p99v, 0.99);
  out.p999_ns = pct(p999v, 0.999);
  out.max_ns = lats.empty() ? 0 : *std::max_element(lats.begin(), lats.end());
  out.page_faults = (prod_after.faults - prod_before.faults) + (con_after.faults - con_before.faults);
  out.ctx_switches = (prod_after.csw - prod_before.csw) + (con_after.csw - con_before.csw);
  return out;
}

std::string utc_stamp() {
  const auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
#if defined(__linux__)
  gmtime_r(&tt, &tm);
#else
  gmtime_s(&tm, &tt);
#endif
  char out[64];
  std::strftime(out, sizeof(out), "%Y%m%dT%H%M%SZ", &tm);
  return out;
}

const char* backing_kind(mf::core::SpscBackingKind k) {
  switch (k) {
    case mf::core::SpscBackingKind::Heap: return "heap";
    case mf::core::SpscBackingKind::Numa: return "numa";
    case mf::core::SpscBackingKind::HugeTlb: return "map_hugetlb";
    case mf::core::SpscBackingKind::MadviseHuge: return "madvise_hugepage";
  }
  return "unknown";
}

}  // namespace

int main(int argc, char** argv) {
  const Config cfg = parse_args(argc, argv);
  constexpr std::size_t kCap = 1U << 20U;

  std::vector<BenchResult> results;
  if (cfg.mode == "baseline" || cfg.mode == "both") results.push_back(run_one<kCap>("baseline", cfg, false));
  if (cfg.mode == "tuned" || cfg.mode == "both") results.push_back(run_one<kCap>("tuned", cfg, true));

  for (const auto& r : results) {
    std::printf("%-8s tput=%.2f p50=%lluns p99=%lluns p99.9=%lluns max=%lluns faults=%llu csw=%llu backing=%s\n",
                r.label.c_str(),
                r.throughput_mps,
                static_cast<unsigned long long>(r.p50_ns),
                static_cast<unsigned long long>(r.p99_ns),
                static_cast<unsigned long long>(r.p999_ns),
                static_cast<unsigned long long>(r.max_ns),
                static_cast<unsigned long long>(r.page_faults),
                static_cast<unsigned long long>(r.ctx_switches),
                backing_kind(r.backing.kind));
  }

  std::string out = cfg.json_out.empty() ? ("bench/results/phase_d_pin_" + utc_stamp() + ".json") : cfg.json_out;
  if (const auto parent = std::filesystem::path(out).parent_path();
      !parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream os(out);
  os << "{";
  for (std::size_t i = 0; i < results.size(); ++i) {
    const auto& r = results[i];
    if (i > 0) os << ",";
    os << "\"" << r.label << "\":{"
       << "\"throughput_mps\":" << r.throughput_mps << ","
       << "\"p50_ns\":" << r.p50_ns << ","
       << "\"p99_ns\":" << r.p99_ns << ","
       << "\"p999_ns\":" << r.p999_ns << ","
       << "\"max_ns\":" << r.max_ns << ","
       << "\"page_faults\":" << r.page_faults << ","
       << "\"context_switches\":" << r.ctx_switches << ","
       << "\"backing\":\"" << backing_kind(r.backing.kind) << "\""
       << "}";
  }
  if (results.size() == 2) {
    const auto& b = results[0];
    const auto& t = results[1];
    os << ",\"delta\":{"
       << "\"throughput_mps\":" << (t.throughput_mps - b.throughput_mps) << ","
       << "\"p50_ns\":" << static_cast<long long>(t.p50_ns) - static_cast<long long>(b.p50_ns) << ","
       << "\"p99_ns\":" << static_cast<long long>(t.p99_ns) - static_cast<long long>(b.p99_ns) << ","
       << "\"p999_ns\":" << static_cast<long long>(t.p999_ns) - static_cast<long long>(b.p999_ns) << ","
       << "\"page_faults\":" << static_cast<long long>(t.page_faults) - static_cast<long long>(b.page_faults) << ","
       << "\"context_switches\":" << static_cast<long long>(t.ctx_switches) - static_cast<long long>(b.ctx_switches)
       << "}";
  }
  os << "}\n";
  std::printf("json=%s\n", out.c_str());
  return 0;
}
