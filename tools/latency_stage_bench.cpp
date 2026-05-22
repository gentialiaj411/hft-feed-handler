#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

#include "mf/bench/histogram.hpp"
#include "mf/bench/run_metadata.hpp"
#include "mf/bench/tsc_clock.hpp"
#include "mf/core/spsc_ring.hpp"
#include "mf/core/types.hpp"
#include "mf/phase2/sequence_tracker.hpp"
#include "mf/phase3/intrusive_order_book.hpp"
#include "mf/phase3/nbbo_consolidator.hpp"
#include "mf/phase3/types.hpp"
#include "mf/proto/nasdaq/itch50_parser.hpp"

namespace {

struct Config {
  std::uint64_t events{1'000'000};
  std::uint64_t warmup{50'000};
  int core{-1};
  std::string out_dir{"bench/results"};
};

struct StageHistograms {
  mf::bench::LatencyHistogram decode{1, 10'000'000, 3};
  mf::bench::LatencyHistogram sequence{1, 10'000'000, 3};
  mf::bench::LatencyHistogram book_apply{1, 10'000'000, 3};
  mf::bench::LatencyHistogram publish{1, 10'000'000, 3};
  mf::bench::LatencyHistogram end_to_end{1, 10'000'000, 3};
};

Config parse_args(int argc, char** argv) {
  Config cfg{};
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--events" && i + 1 < argc) {
      cfg.events = std::stoull(argv[++i]);
    } else if (arg == "--warmup" && i + 1 < argc) {
      cfg.warmup = std::stoull(argv[++i]);
    } else if (arg == "--core" && i + 1 < argc) {
      cfg.core = std::stoi(argv[++i]);
    } else if (arg == "--out-dir" && i + 1 < argc) {
      cfg.out_dir = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "usage: latency_stage_bench [--events N] [--warmup N] [--core N] [--out-dir DIR]\n";
      std::exit(0);
    }
  }
  return cfg;
}

bool pin_to_core(int core) {
  if (core < 0) return false;
#if defined(_WIN32)
  if (core >= static_cast<int>(sizeof(DWORD_PTR) * 8U)) return false;
  return SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR{1} << static_cast<unsigned>(core))) != 0;
#elif defined(__linux__)
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(core, &set);
  return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
#else
  (void)core;
  return false;
#endif
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

std::vector<std::byte> make_itch_add(std::uint64_t i) {
  std::vector<std::byte> p(36);
  p[0] = std::byte{'A'};
  const std::uint64_t ts = 1'000'000'000ULL + i * 100ULL;
  for (int b = 0; b < 6; ++b) p[1 + b] = std::byte{static_cast<unsigned char>((ts >> ((5 - b) * 8)) & 0xffU)};
  const std::uint64_t order_id = i + 1U;
  for (int b = 0; b < 8; ++b) p[11 + b] = std::byte{static_cast<unsigned char>((order_id >> ((7 - b) * 8)) & 0xffU)};
  p[19] = ((i & 1U) == 0U) ? std::byte{'B'} : std::byte{'S'};
  const std::uint32_t qty = 100U + static_cast<std::uint32_t>(i & 15U);
  for (int b = 0; b < 4; ++b) p[20 + b] = std::byte{static_cast<unsigned char>((qty >> ((3 - b) * 8)) & 0xffU)};
  const std::array<char, 8> symbol = {'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '};
  for (std::size_t b = 0; b < symbol.size(); ++b) p[24 + b] = std::byte{static_cast<unsigned char>(symbol[b])};
  const std::uint32_t price = 10'000U + static_cast<std::uint32_t>(i & 31U);
  for (int b = 0; b < 4; ++b) p[32 + b] = std::byte{static_cast<unsigned char>((price >> ((3 - b) * 8)) & 0xffU)};
  return p;
}

void record_percentiles(std::ostream& os, const char* name, const mf::bench::LatencyHistogram& h) {
  os << "| " << name
     << " | " << h.count()
     << " | " << h.percentile(0.50)
     << " | " << h.percentile(0.99)
     << " | " << h.percentile(0.999)
     << " | " << h.percentile(0.9999)
     << " | " << h.max()
     << " |\n";
}

}  // namespace

int main(int argc, char** argv) {
  const Config cfg = parse_args(argc, argv);
  const bool pinned = pin_to_core(cfg.core);
  const double ticks_per_ns = mf::bench::calibrate_ticks_per_ns();
  auto meta = mf::bench::capture_run_metadata(argc, argv);

  mf::proto::nasdaq::Itch50Parser parser;
  mf::proto::nasdaq::ParseStats parse_stats{};
  mf::phase2::MultiVenueSequenceTracker sequencer(/*gap_window=*/256);
  mf::phase3::IntrusiveOrderBook book;
  mf::phase3::NbboConsolidator nbbo;
  auto ring = std::make_unique<mf::core::SPSCRingBuffer<mf::phase3::FeatureVector, 1U << 16U>>();
  mf::phase3::FeatureVector published{};
  StageHistograms hist{};

  const auto total = cfg.warmup + cfg.events;
  std::uint64_t decoded = 0;
  std::uint64_t published_count = 0;

  for (std::uint64_t i = 0; i < total; ++i) {
    auto payload = make_itch_add(i);
    const bool measure = i >= cfg.warmup;
    const auto t0 = mf::bench::tsc_now();
    auto ev = parser.parse_message(payload, (i / 3U) + 1U, t0, parse_stats);
    const auto t1 = mf::bench::tsc_now();
    if (!ev.has_value()) continue;
    ev->venue = static_cast<mf::core::Venue>(i % 3U);
    ev->sequence = (i / 3U) + 1U;
    ++decoded;

    const auto seq = sequencer.on_sequence(ev->venue, ev->sequence);
    const auto t2 = mf::bench::tsc_now();
    if (seq.status != mf::phase2::SequenceStatus::InOrder) continue;

    const auto apply = book.on_event(*ev);
    (void)nbbo.update(ev->symbol.as_u64(), ev->venue, apply.top_after);
    const auto t3 = mf::bench::tsc_now();

    mf::phase3::FeatureVector fv{};
    fv.symbol_u64 = ev->symbol.as_u64();
    fv.exchange_ts_ns = ev->exchange_ts_ns;
    fv.ingest_ts_ns = ev->ingest_ts_ns;
    fv.nbbo_bid_price = apply.top_after.bid_price;
    fv.nbbo_bid_qty = apply.top_after.bid_qty;
    fv.nbbo_ask_price = apply.top_after.ask_price;
    fv.nbbo_ask_qty = apply.top_after.ask_qty;
    if (ring->try_push(fv)) {
      ++published_count;
    }
    while (ring->try_pop(published)) {}
    const auto t4 = mf::bench::tsc_now();

    if (measure) {
      hist.decode.record(mf::bench::ticks_to_ns(t1 - t0, ticks_per_ns));
      hist.sequence.record(mf::bench::ticks_to_ns(t2 - t1, ticks_per_ns));
      hist.book_apply.record(mf::bench::ticks_to_ns(t3 - t2, ticks_per_ns));
      hist.publish.record(mf::bench::ticks_to_ns(t4 - t3, ticks_per_ns));
      hist.end_to_end.record(mf::bench::ticks_to_ns(t4 - t0, ticks_per_ns));
    }
  }

  std::filesystem::create_directories(cfg.out_dir);
  const auto base = cfg.out_dir + "/latency_report_" + stamp();
  const auto md_path = base + ".md";
  const auto json_path = base + ".json";

  {
    std::ofstream os(md_path);
    os << "# Latency Report\n\n";
    os << "- events_measured: " << cfg.events << "\n";
    os << "- warmup_discarded: " << cfg.warmup << "\n";
    os << "- decoded_total: " << decoded << "\n";
    os << "- published_total: " << published_count << "\n";
    os << "- timing_source: RDTSCP when available, steady_clock fallback otherwise\n";
    os << "- ticks_per_ns: " << std::fixed << std::setprecision(6) << ticks_per_ns << "\n";
    os << "- requested_core: " << cfg.core << "\n";
    os << "- pinned: " << (pinned ? "true" : "false") << "\n";
    os << "- methodology: warmup discarded, single producer thread, synthetic ITCH Add payloads, cache-warm steady-state loop\n\n";
    os << "| stage | count | p50_ns | p99_ns | p99.9_ns | p99.99_ns | max_ns |\n";
    os << "|---|---:|---:|---:|---:|---:|---:|\n";
    record_percentiles(os, "decode", hist.decode);
    record_percentiles(os, "sequence", hist.sequence);
    record_percentiles(os, "book_apply", hist.book_apply);
    record_percentiles(os, "publish", hist.publish);
    record_percentiles(os, "end_to_end", hist.end_to_end);
  }

  {
    std::ofstream os(json_path);
    os << "{\"metadata\":" << mf::bench::run_metadata_to_json(meta)
       << ",\"events\":" << cfg.events
       << ",\"warmup\":" << cfg.warmup
       << ",\"ticks_per_ns\":" << ticks_per_ns
       << ",\"pinned\":" << (pinned ? "true" : "false")
       << ",\"histograms\":{"
       << "\"decode\":" << hist.decode.to_json_buckets() << ","
       << "\"sequence\":" << hist.sequence.to_json_buckets() << ","
       << "\"book_apply\":" << hist.book_apply.to_json_buckets() << ","
       << "\"publish\":" << hist.publish.to_json_buckets() << ","
       << "\"end_to_end\":" << hist.end_to_end.to_json_buckets()
       << "}}\n";
  }

  std::cout << "latency_report=" << md_path << "\n";
  std::cout << "latency_json=" << json_path << "\n";
  return 0;
}
