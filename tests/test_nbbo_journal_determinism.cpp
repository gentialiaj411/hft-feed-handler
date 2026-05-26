#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/journal/journal_reader.hpp"
#include "mf/phase3/nbbo_journal_pipeline.hpp"

#if defined(__linux__)
#include <unistd.h>
#endif

namespace {

mf::core::BookEvent make_add(
    mf::core::Venue venue,
    std::uint64_t seq,
    std::uint64_t ts,
    mf::core::Side side,
    std::uint32_t price,
    std::uint64_t order_id) {
  mf::core::BookEvent ev{};
  ev.venue = venue;
  ev.type = mf::core::EventType::Add;
  ev.sequence = seq;
  ev.exchange_ts_ns = ts;
  ev.ingest_ts_ns = ts + 100;
  ev.side = side;
  ev.price = price;
  ev.qty = 100;
  ev.order_id = order_id;
  ev.symbol.bytes = {'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '};
  return ev;
}

}  // namespace

int main() {
#if !defined(__linux__)
  std::printf("SKIP: NBBO journal determinism test is Linux-only\n");
  return 0;
#else
  std::vector<mf::core::BookEvent> events;
  events.push_back(make_add(mf::core::Venue::Nasdaq, 1, 1000, mf::core::Side::Buy, 10000, 1));
  events.push_back(make_add(mf::core::Venue::Nasdaq, 2, 1001, mf::core::Side::Sell, 10020, 2));
  events.push_back(make_add(mf::core::Venue::Iex, 3, 1002, mf::core::Side::Buy, 10010, 3));
  events.push_back(make_add(mf::core::Venue::Cboe, 4, 1003, mf::core::Side::Sell, 10015, 4));
  events.push_back(make_add(mf::core::Venue::Iex, 5, 1004, mf::core::Side::Sell, 10012, 5));

  char path_a[] = "/tmp/mf_nbbo_a_XXXXXX";
  char path_b[] = "/tmp/mf_nbbo_b_XXXXXX";
  const int fa = ::mkstemp(path_a);
  const int fb = ::mkstemp(path_b);
  assert(fa >= 0 && fb >= 0);
  ::close(fa);
  ::close(fb);

  mf::phase3::NbboJournalPipeline pipeline;
  mf::phase3::NbboJournalPipelineStats stats_a{};
  mf::phase3::NbboJournalPipelineStats stats_b{};
  assert(pipeline.replay_to_journal(events, path_a, stats_a));
  assert(pipeline.replay_to_journal(events, path_b, stats_b));
  assert(stats_a.nbbo_emitted > 0);
  assert(stats_a.nbbo_emitted == stats_b.nbbo_emitted);
  assert(stats_a.journal_crc == stats_b.journal_crc);

  const char* full_journal = std::getenv("MF_NBBO_SOURCE_JOURNAL");
  if (full_journal != nullptr && full_journal[0] != '\0') {
    mf::journal::JournalReader book_reader;
    if (book_reader.open(full_journal)) {
      std::vector<mf::core::BookEvent> full_events;
      mf::core::BookEvent ev{};
      std::uint64_t ingest_ts = 0;
      std::uint64_t seq = 0;
      while (book_reader.next(ev, ingest_ts, seq)) {
        ev.ingest_ts_ns = ingest_ts;
        full_events.push_back(ev);
      }
      char path_c[] = "/tmp/mf_nbbo_c_XXXXXX";
      char path_d[] = "/tmp/mf_nbbo_d_XXXXXX";
      const int fc = ::mkstemp(path_c);
      const int fd = ::mkstemp(path_d);
      assert(fc >= 0 && fd >= 0);
      ::close(fc);
      ::close(fd);
      mf::phase3::NbboJournalPipelineStats stats_c{};
      mf::phase3::NbboJournalPipelineStats stats_d{};
      assert(pipeline.replay_to_journal(full_events, path_c, stats_c));
      assert(pipeline.replay_to_journal(full_events, path_d, stats_d));
      std::ifstream fa(path_c, std::ios::binary);
      std::ifstream fb(path_d, std::ios::binary);
      const std::vector<char> bytes_a((std::istreambuf_iterator<char>(fa)), std::istreambuf_iterator<char>());
      const std::vector<char> bytes_b((std::istreambuf_iterator<char>(fb)), std::istreambuf_iterator<char>());
      assert(bytes_a == bytes_b);
      ::unlink(path_c);
      ::unlink(path_d);
      std::printf("full_journal_byte_identical records=%zu nbbo=%llu\n",
          full_events.size(),
          static_cast<unsigned long long>(stats_c.nbbo_emitted));
    }
  }

  ::unlink(path_a);
  ::unlink(path_b);
  std::printf("PASS nbbo_journal_determinism emitted=%llu crc=0x%08x\n",
      static_cast<unsigned long long>(stats_a.nbbo_emitted),
      stats_a.journal_crc);
  return 0;
#endif
}
