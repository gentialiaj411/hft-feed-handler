#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "mf/bench/histogram.hpp"
#include "mf/core/time.hpp"
#include "mf/journal/journal_reader.hpp"
#include "mf/phase3/nbbo_consolidator.hpp"
#include "mf/phase3/nbbo_journal_pipeline.hpp"
#include "mf/phase3/order_book_engine.hpp"

namespace {
std::string arg(int argc, char** argv, const std::string& key, const std::string& dflt) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] == key) return argv[i + 1];
  }
  return dflt;
}

std::uint64_t arg_u64(int argc, char** argv, const std::string& key, std::uint64_t dflt) {
  const std::string v = arg(argc, argv, key, "");
  if (v.empty()) return dflt;
  return static_cast<std::uint64_t>(std::stoull(v));
}
}  // namespace

int main(int argc, char** argv) {
#if !defined(__linux__)
  std::printf("nbbo_replay_bench is Linux-only\n");
  return 0;
#else
  const std::string in_path = arg(argc, argv, "--in", "");
  const std::string out_journal = arg(argc, argv, "--out-journal", "/tmp/mf_nbbo_bench.journal");
  const std::string out_md = arg(argc, argv, "--out-md", "bench/results/nbbo_replay.md");
  const std::uint64_t max_events = arg_u64(argc, argv, "--max-events", 0);
  if (in_path.empty()) {
    std::fprintf(stderr,
        "usage: nbbo_replay_bench --in <book.journal> [--out-journal path] [--out-md path] [--max-events N]\n");
    return 2;
  }

  mf::journal::JournalReader reader;
  if (!reader.open(in_path)) {
    std::fprintf(stderr, "failed to open input\n");
    return 1;
  }

  std::vector<mf::core::BookEvent> events;
  mf::core::BookEvent ev{};
  std::uint64_t ingest_ts = 0;
  std::uint64_t seq = 0;
  mf::bench::LatencyHistogram emit_hist;
  mf::phase3::OrderBookEngine books;
  mf::phase3::NbboConsolidator nbbo;
  std::uint64_t nbbo_emitted = 0;

  const auto t0 = std::chrono::steady_clock::now();
  while (reader.next(ev, ingest_ts, seq)) {
    ev.ingest_ts_ns = ingest_ts;
    if (max_events > 0 && events.size() >= max_events) {
      break;
    }
    events.push_back(ev);
    const auto apply = books.on_event(ev);
    if (!apply.changed_top) {
      continue;
    }
    const std::uint64_t t_emit0 = mf::core::monotonic_raw_now_ns();
    if (nbbo.update(ev.symbol.as_u64(), ev.venue, apply.top_after, ev.sequence)) {
      const std::uint64_t t_emit1 = mf::core::monotonic_raw_now_ns();
      if (t_emit1 > t_emit0) {
        emit_hist.record(t_emit1 - t_emit0);
      }
      ++nbbo_emitted;
    }
  }
  const auto t1 = std::chrono::steady_clock::now();
  const double wall_sec = std::chrono::duration<double>(t1 - t0).count();

  mf::phase3::NbboJournalPipeline pipeline;
  mf::phase3::NbboJournalPipelineStats stats{};
  if (!pipeline.replay_to_journal(events, out_journal, stats)) {
    std::fprintf(stderr, "journal write failed\n");
    return 1;
  }

  const double events_per_sec = (wall_sec > 0.0) ? static_cast<double>(events.size()) / wall_sec : 0.0;
  const double nbbo_per_sec = (wall_sec > 0.0) ? static_cast<double>(nbbo_emitted) / wall_sec : 0.0;

  std::FILE* md = std::fopen(out_md.c_str(), "w");
  if (md != nullptr) {
    std::fprintf(md, "# NBBO replay bench\n\n");
    std::fprintf(md, "Input: `%s`. Pinning: optional, see `docs/runbook-pinning.md`.\n\n", in_path.c_str());
    std::fprintf(md, "| metric | value |\n|---|---|\n");
    std::fprintf(md, "| book_events | %zu |\n", events.size());
    std::fprintf(md, "| nbbo_emitted | %llu |\n", static_cast<unsigned long long>(stats.nbbo_emitted));
    std::fprintf(md, "| events_per_sec | %.2f |\n", events_per_sec);
    std::fprintf(md, "| nbbo_events_per_sec | %.2f |\n", nbbo_per_sec);
    std::fprintf(md, "| p50_emit_latency_ns | %llu |\n", static_cast<unsigned long long>(emit_hist.percentile(0.50)));
    std::fprintf(md, "| p99_emit_latency_ns | %llu |\n", static_cast<unsigned long long>(emit_hist.percentile(0.99)));
    std::fprintf(md, "| journal_crc | 0x%08x |\n", stats.journal_crc);
    std::fprintf(md, "| output_journal | %s |\n", out_journal.c_str());
    std::fclose(md);
  }

  std::printf("book_events=%zu nbbo_emitted=%llu journal_crc=0x%08x p50=%llu p99=%llu\n",
      events.size(),
      static_cast<unsigned long long>(stats.nbbo_emitted),
      stats.journal_crc,
      static_cast<unsigned long long>(emit_hist.percentile(0.50)),
      static_cast<unsigned long long>(emit_hist.percentile(0.99)));
  return 0;
#endif
}
