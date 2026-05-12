#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/phase2/pipeline.hpp"

namespace {

struct BenchConfig {
  std::size_t events{2'000'000};
  std::uint64_t gap_window{256};
  std::size_t per_venue_capacity{1U << 20U};
};

BenchConfig parse_args(int argc, char** argv) {
  BenchConfig cfg{};
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--events" && i + 1 < argc) {
      cfg.events = static_cast<std::size_t>(std::stoull(argv[++i]));
    } else if (a == "--gap-window" && i + 1 < argc) {
      cfg.gap_window = static_cast<std::uint64_t>(std::stoull(argv[++i]));
    } else if (a == "--capacity" && i + 1 < argc) {
      cfg.per_venue_capacity = static_cast<std::size_t>(std::stoull(argv[++i]));
    } else if (a == "--help" || a == "-h") {
      std::cout << "usage: phase2_pipeline_bench [--events N] [--gap-window N] [--capacity N]\n";
      std::exit(0);
    }
  }
  return cfg;
}

mf::core::BookEvent make_event(
    mf::core::Venue venue,
    std::uint64_t sequence,
    std::uint64_t ts) {
  mf::core::BookEvent ev{};
  ev.venue = venue;
  ev.type = mf::core::EventType::Add;
  ev.sequence = sequence;
  ev.exchange_ts_ns = ts;
  ev.ingest_ts_ns = ts + 10U;
  ev.qty = 100U;
  ev.price = 10000U + static_cast<std::uint32_t>(sequence & 0xFFU);
  ev.raw_type = static_cast<std::uint8_t>('A');
  ev.order_id = sequence;
  return ev;
}

}  // namespace

int main(int argc, char** argv) {
  const BenchConfig cfg = parse_args(argc, argv);

  // Generate deterministic synthetic flow with occasional bounded out-of-order events.
  std::vector<mf::core::BookEvent> events;
  events.reserve(cfg.events);
  std::uint64_t seq_n = 1;
  std::uint64_t seq_i = 1;
  std::uint64_t seq_c = 1;
  std::uint64_t ts = 1'000'000'000ULL;

  for (std::size_t i = 0; i < cfg.events; ++i) {
    mf::core::Venue v = mf::core::Venue::Nasdaq;
    std::uint64_t seq = 0;
    if ((i % 3U) == 0U) {
      v = mf::core::Venue::Nasdaq;
      seq = seq_n++;
    } else if ((i % 3U) == 1U) {
      v = mf::core::Venue::Iex;
      seq = seq_i++;
    } else {
      v = mf::core::Venue::Cboe;
      seq = seq_c++;
    }

    events.push_back(make_event(v, seq, ts));
    ts += 100U;

    // Inject deterministic short reordering: emit seq+1 before seq every 50k events.
    if ((i % 50'000U) == 0U) {
      if (v == mf::core::Venue::Nasdaq) {
        events.push_back(make_event(v, seq_n + 1U, ts + 100U));
        events.push_back(make_event(v, seq_n, ts + 200U));
        seq_n += 2U;
      }
    }
  }

  mf::phase2::Pipeline pipeline(cfg.gap_window, cfg.per_venue_capacity);
  const auto t0 = std::chrono::steady_clock::now();
  for (const auto& ev : events) {
    pipeline.on_event(ev);
  }
  pipeline.finalize();
  const auto t1 = std::chrono::steady_clock::now();

  const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
  const double elapsed_s = static_cast<double>(elapsed_ns) / 1e9;
  const double eps = elapsed_s > 0.0 ? static_cast<double>(events.size()) / elapsed_s : 0.0;
  const auto& s = pipeline.stats();

  std::cout << "[phase2_bench]\n";
  std::cout << "events_in=" << events.size() << "\n";
  std::cout << "elapsed_ns=" << elapsed_ns << "\n";
  std::cout << "events_per_sec=" << std::fixed << std::setprecision(2) << eps << "\n";
  std::cout << "accepted=" << s.accepted << "\n";
  std::cout << "dropped_publish_overflow=" << s.dropped_publish_overflow << "\n";
  std::cout << "dropped_duplicate_or_old=" << s.dropped_duplicate_or_old << "\n";
  std::cout << "buffered_out_of_order=" << s.buffered_out_of_order << "\n";
  std::cout << "dropped_gap_too_large=" << s.dropped_gap_too_large << "\n";
  std::cout << "dropped_gap_too_large_pending_evicted=" << s.dropped_gap_too_large_pending_evicted << "\n";
  std::cout << "recovery_requests=" << s.recovery_requests << "\n";
  std::cout << "recovery_reinjected=" << s.recovery_reinjected << "\n";
  std::cout << "merged_crc32=0x" << std::hex << std::setw(8) << std::setfill('0') << s.merged_crc << std::dec << "\n";
  return 0;
}
