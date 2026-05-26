#include <cassert>
#include <filesystem>
#include <cstdint>

#include "mf/core/types.hpp"
#include "mf/phase2/pipeline.hpp"
#if defined(__linux__)
#include "mf/journal/journal_reader.hpp"
#endif

namespace {

mf::core::BookEvent make_event(mf::core::Venue venue, std::uint64_t seq, std::uint64_t ts) {
  mf::core::BookEvent ev{};
  ev.venue = venue;
  ev.sequence = seq;
  ev.exchange_ts_ns = ts;
  ev.type = mf::core::EventType::Add;
  ev.raw_type = static_cast<std::uint8_t>('A');
  ev.qty = 10;
  ev.price = 100;
  return ev;
}

void test_out_of_order_then_recovery_reinject() {
  mf::phase2::Pipeline pipeline(/*gap_window=*/8, /*per_venue_capacity=*/64);

  // Sequence 1 accepted.
  pipeline.on_event(make_event(mf::core::Venue::Nasdaq, 1, 1000));
  // Sequence 3 arrives first: buffered + recovery request for [2..2].
  pipeline.on_event(make_event(mf::core::Venue::Nasdaq, 3, 1002));
  // Later the missing event appears in replay stream and is stored/reinjected path ready.
  pipeline.on_event(make_event(mf::core::Venue::Nasdaq, 2, 1001));

  pipeline.finalize();
  const auto& s = pipeline.stats();
  assert(s.accepted == 3);
  assert(s.buffered_out_of_order == 1);
  assert(s.recovery_requests >= 1);
  assert(s.merged_crc != 0);
}

void test_gap_too_large_drop() {
  mf::phase2::Pipeline pipeline(/*gap_window=*/1, /*per_venue_capacity=*/64);
  pipeline.on_event(make_event(mf::core::Venue::Iex, 1, 2000));
  pipeline.on_event(make_event(mf::core::Venue::Iex, 3, 2003));
  pipeline.on_event(make_event(mf::core::Venue::Iex, 10, 2010));
  pipeline.on_event(make_event(mf::core::Venue::Iex, 10, 2010));
  pipeline.on_event(make_event(mf::core::Venue::Iex, 11, 2011));
  pipeline.finalize();
  const auto& s = pipeline.stats();
  assert(s.dropped_gap_too_large == 1);
  assert(s.dropped_gap_too_large_pending_evicted == 1);
  assert(s.accepted == 3);
}

void test_publish_overflow_counted() {
  mf::phase2::Pipeline pipeline(/*gap_window=*/8, /*per_venue_capacity=*/1);
  pipeline.on_event(make_event(mf::core::Venue::Nasdaq, 1, 1000));
  pipeline.on_event(make_event(mf::core::Venue::Nasdaq, 2, 1001));
  pipeline.finalize();
  const auto& s = pipeline.stats();
  assert(s.accepted == 2);
  assert(s.dropped_publish_overflow == 0);
}

void test_long_single_venue_stream_no_overflow() {
  constexpr std::size_t kEvents = 2'000'000;

  mf::phase2::Pipeline reference(/*gap_window=*/8, /*per_venue_capacity=*/0);
  mf::phase2::Pipeline candidate(/*gap_window=*/8, /*per_venue_capacity=*/1);

  for (std::size_t i = 1; i <= kEvents; ++i) {
    const auto seq = static_cast<std::uint64_t>(i);
    const auto ts = static_cast<std::uint64_t>(1'000'000 + i);
    const auto ev = make_event(mf::core::Venue::Nasdaq, seq, ts);
    reference.on_event(ev);
    candidate.on_event(ev);
  }

  reference.finalize();
  candidate.finalize();

  const auto& ref = reference.stats();
  const auto& cand = candidate.stats();
  assert(ref.merged_crc != 0);
  assert(cand.dropped_publish_overflow == 0);
  assert(cand.accepted == kEvents);
  assert(cand.merged_crc == ref.merged_crc);
}

#if defined(__linux__)
void test_full_day_journal_no_publish_overflow() {
  const auto repo_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto journal_path = repo_root / "bench" / "data" / "itch_full_day_20190130.journal";
  assert(std::filesystem::exists(journal_path));

  mf::journal::JournalReader reader;
  assert(reader.open(journal_path.string()));

  mf::phase2::Pipeline pipeline(/*gap_window=*/1024, /*per_venue_capacity=*/1U << 20U);
  mf::core::BookEvent ev{};
  std::uint64_t ts = 0;
  std::uint64_t seq = 0;
  while (reader.next(ev, ts, seq)) {
    ev.ingest_ts_ns = ts;
    pipeline.on_event(ev);
  }
  pipeline.finalize();

  const auto& jr = reader.stats();
  const auto& s = pipeline.stats();
  assert(!reader.had_error());
  assert(jr.crc_failures == 0);
  assert(s.dropped_publish_overflow == 0);
  assert(s.merged_crc == 0xa5dd7c07U);
  assert(s.accepted + s.dropped_duplicate_or_old + s.dropped_gap_too_large == jr.records_read);
}
#endif

}  // namespace

int main() {
  test_out_of_order_then_recovery_reinject();
  test_gap_too_large_drop();
  test_publish_overflow_counted();
  test_long_single_venue_stream_no_overflow();
#if defined(__linux__)
  test_full_day_journal_no_publish_overflow();
#endif
  return 0;
}
