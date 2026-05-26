#include <cstdio>
#include <string>
#include <vector>

#include "mf/journal/journal_reader.hpp"
#include "mf/phase3/nbbo_journal_pipeline.hpp"

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
  std::printf("nbbo_journal_from_book is Linux-only\n");
  return 0;
#else
  const std::string in_path = arg(argc, argv, "--in", "");
  const std::string out_path = arg(argc, argv, "--out", "");
  const std::uint64_t max_events = arg_u64(argc, argv, "--max-events", 0);
  if (in_path.empty() || out_path.empty()) {
    std::fprintf(stderr, "usage: nbbo_journal_from_book --in <book.journal> --out <nbbo.journal> [--max-events N]\n");
    return 2;
  }

  mf::journal::JournalReader reader;
  if (!reader.open(in_path)) {
    std::fprintf(stderr, "failed to open input journal\n");
    return 1;
  }

  std::vector<mf::core::BookEvent> events;
  events.reserve(static_cast<std::size_t>(max_events > 0 ? max_events : 1'000'000));
  mf::core::BookEvent ev{};
  std::uint64_t ingest_ts = 0;
  std::uint64_t seq = 0;
  while (reader.next(ev, ingest_ts, seq)) {
    ev.ingest_ts_ns = ingest_ts;
    events.push_back(ev);
    if (max_events > 0 && events.size() >= max_events) {
      break;
    }
  }
  if (reader.stats().crc_failures != 0) {
    std::fprintf(stderr, "input crc failures=%llu\n",
        static_cast<unsigned long long>(reader.stats().crc_failures));
    return 1;
  }

  mf::phase3::NbboJournalPipeline pipeline;
  mf::phase3::NbboJournalPipelineStats stats{};
  if (!pipeline.replay_to_journal(events, out_path, stats)) {
    std::fprintf(stderr, "failed to write nbbo journal\n");
    return 1;
  }

  std::printf("book_events=%llu nbbo_emitted=%llu top_changes=%llu journal_crc=0x%08x\n",
      static_cast<unsigned long long>(stats.book_events_in),
      static_cast<unsigned long long>(stats.nbbo_emitted),
      static_cast<unsigned long long>(stats.top_changes),
      stats.journal_crc);
  return 0;
#endif
}
