#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#include <windows.h>
#endif

#include "mf/core/spsc_ring.hpp"
#include "mf/core/types.hpp"
#include "mf/phase2/sequence_tracker.hpp"

namespace {

struct Config {
  std::size_t events{2'000'000};
  std::size_t warmup{10'000};
};

Config parse_args(int argc, char** argv) {
  Config cfg{};
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--events" && i + 1 < argc) {
      cfg.events = static_cast<std::size_t>(std::stoull(argv[++i]));
    } else if (arg == "--warmup" && i + 1 < argc) {
      cfg.warmup = static_cast<std::size_t>(std::stoull(argv[++i]));
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "usage: feed_hot_path_bench [--events N] [--warmup N]\n";
      std::exit(0);
    }
  }
  return cfg;
}

void pin_to_core_zero() {
#if defined(_WIN32)
  (void)SetThreadAffinityMask(GetCurrentThread(), 1ULL);
#endif
}

std::uint64_t ticks_now() noexcept {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
  return __rdtsc();
#else
  return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

double calibrate_ticks_per_ns() {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
  LARGE_INTEGER freq{};
  LARGE_INTEGER q0{};
  LARGE_INTEGER q1{};
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&q0);
  const std::uint64_t t0 = ticks_now();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const std::uint64_t t1 = ticks_now();
  QueryPerformanceCounter(&q1);
  const double elapsed_ns = static_cast<double>(q1.QuadPart - q0.QuadPart) *
                            1'000'000'000.0 / static_cast<double>(freq.QuadPart);
  return static_cast<double>(t1 - t0) / elapsed_ns;
#else
  return 1.0;
#endif
}

mf::core::BookEvent make_event(std::size_t i) {
  mf::core::BookEvent ev{};
  ev.venue = static_cast<mf::core::Venue>(i % 3U);
  ev.sequence = static_cast<std::uint64_t>((i / 3U) + 1U);
  ev.exchange_ts_ns = 1'000'000'000ULL + static_cast<std::uint64_t>(i * 100U);
  ev.ingest_ts_ns = ev.exchange_ts_ns + 10U;
  ev.symbol.bytes = {'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '};
  ev.order_id = static_cast<std::uint64_t>(i + 1U);
  ev.type = mf::core::EventType::Add;
  ev.side = (i & 1U) == 0U ? mf::core::Side::Buy : mf::core::Side::Sell;
  ev.price = 10'000U + static_cast<std::uint32_t>(i & 31U);
  ev.qty = 100U + static_cast<std::uint32_t>(i & 15U);
  ev.raw_type = static_cast<std::uint8_t>('A');
  return ev;
}

std::uint64_t pct(std::vector<std::uint64_t>& values, double p) {
  const std::size_t idx = static_cast<std::size_t>(
      p * static_cast<double>(values.empty() ? 0U : values.size() - 1U));
  std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(idx), values.end());
  return values[idx];
}

}  // namespace

int main(int argc, char** argv) {
  const Config cfg = parse_args(argc, argv);
  pin_to_core_zero();
  const double ticks_per_ns = calibrate_ticks_per_ns();

  auto ring = std::make_unique<mf::core::SPSCRingBuffer<mf::core::BookEvent, 1U << 20U>>();
  mf::phase2::MultiVenueSequenceTracker seq(/*gap_window=*/256);
  mf::core::BookEvent sink{};

  for (std::size_t i = 0; i < cfg.warmup; ++i) {
    const auto ev = make_event(i);
    const auto status = seq.on_sequence(ev.venue, ev.sequence);
    if (status.status == mf::phase2::SequenceStatus::InOrder) {
      (void)ring->try_push(ev);
    }
    while (ring->try_pop(sink)) {}
  }

  std::vector<std::uint64_t> samples;
  samples.reserve(cfg.events);
  std::uint64_t published = 0;
  std::uint64_t dropped = 0;
  for (std::size_t i = 0; i < cfg.events; ++i) {
    const auto ev = make_event(i + cfg.warmup);
    const std::uint64_t t0 = ticks_now();
    const auto status = seq.on_sequence(ev.venue, ev.sequence);
    if (status.status == mf::phase2::SequenceStatus::InOrder && ring->try_push(ev)) {
      ++published;
    } else {
      ++dropped;
    }
    while (ring->try_pop(sink)) {}
    const std::uint64_t t1 = ticks_now();
    samples.push_back(static_cast<std::uint64_t>(static_cast<double>(t1 - t0) / ticks_per_ns));
  }

  auto p50_values = samples;
  auto p99_values = samples;
  auto p999_values = samples;
  const auto max_value = *std::max_element(samples.begin(), samples.end());
  std::cout << "[feed_hot_path]\n";
  std::cout << "events=" << cfg.events << "\n";
  std::cout << "warmup=" << cfg.warmup << "\n";
  std::cout << "published=" << published << "\n";
  std::cout << "dropped=" << dropped << "\n";
  std::cout << "ticks_per_ns=" << std::fixed << std::setprecision(6) << ticks_per_ns << "\n";
  std::cout << "lat_ns_p50=" << pct(p50_values, 0.50) << "\n";
  std::cout << "lat_ns_p99=" << pct(p99_values, 0.99) << "\n";
  std::cout << "lat_ns_p999=" << pct(p999_values, 0.999) << "\n";
  std::cout << "lat_ns_max=" << max_value << "\n";
  return 0;
}
