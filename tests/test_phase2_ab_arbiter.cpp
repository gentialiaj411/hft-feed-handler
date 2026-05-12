#include <cassert>
#include <cstdint>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/phase2/ab_arbiter.hpp"
#include "mf/phase2/pipeline.hpp"

namespace {

mf::core::BookEvent make_event(mf::core::Venue venue, std::uint64_t seq, std::uint64_t ts) {
  mf::core::BookEvent ev{};
  ev.venue = venue;
  ev.sequence = seq;
  ev.exchange_ts_ns = ts;
  ev.ingest_ts_ns = ts + 10U;
  ev.type = mf::core::EventType::Add;
  ev.side = mf::core::Side::Buy;
  ev.price = static_cast<std::uint32_t>(10000U + seq);
  ev.qty = 10;
  ev.order_id = seq;
  ev.raw_type = static_cast<std::uint8_t>('A');
  ev.symbol.bytes = {'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '};
  return ev;
}

void test_first_wins_duplicate_dropped() {
  mf::phase2::AbArbiter arb(8);
  const auto ev = make_event(mf::core::Venue::Nasdaq, 1, 1000);
  const auto r1 = arb.on_event(mf::phase2::FeedSide::A, ev);
  const auto r2 = arb.on_event(mf::phase2::FeedSide::B, ev);
  assert(r1.accepted);
  assert(!r2.accepted);
  assert(r2.status == mf::phase2::SequenceStatus::DuplicateOrOld);
  const auto ready = arb.drain_ready();
  assert(ready.size() == 1);
  assert(ready[0].sequence == 1);
}

void test_drop_injection_and_pipeline_crc_non_zero() {
  std::vector<mf::core::BookEvent> src;
  src.reserve(256);
  for (std::uint64_t i = 1; i <= 256; ++i) {
    src.push_back(make_event(mf::core::Venue::Iex, i, 1'000'000 + i));
  }

  mf::phase2::DroppedFeedCounts counts{};
  const auto raced = mf::phase2::make_dual_feed_race_stream(
      src,
      mf::phase2::DualFeedDropConfig{0.2, 0.2, 7},
      &counts);
  assert(!raced.empty());
  assert(counts.dropped_a > 0);
  assert(counts.dropped_b > 0);

  mf::phase2::AbArbiter arb(32);
  mf::phase2::Pipeline p(/*gap_window=*/32, /*per_venue_capacity=*/1024);
  for (const auto& item : raced) {
    (void)arb.on_event(item.first, item.second);
    auto ready = arb.drain_ready();
    for (const auto& ev : ready) {
      p.on_event(ev);
    }
  }
  p.finalize();
  const auto& s = p.stats();
  assert(s.accepted > 0);
  assert(s.merged_crc != 0);
}

void test_complementary_ab_drops_preserve_crc() {
  std::vector<mf::core::BookEvent> src;
  src.reserve(200);
  for (std::uint64_t i = 1; i <= 200; ++i) {
    src.push_back(make_event(mf::core::Venue::Nasdaq, i, 2'000'000 + i));
  }

  mf::phase2::Pipeline baseline(/*gap_window=*/32, /*per_venue_capacity=*/1024);
  for (const auto& ev : src) {
    baseline.on_event(ev);
  }
  baseline.finalize();
  const std::uint32_t crc_ref = baseline.stats().merged_crc;

  std::vector<std::pair<mf::phase2::FeedSide, mf::core::BookEvent>> raced;
  raced.reserve(src.size());
  for (const auto& ev : src) {
    raced.push_back({((ev.sequence & 1ULL) == 0ULL) ? mf::phase2::FeedSide::A : mf::phase2::FeedSide::B, ev});
  }

  mf::phase2::AbArbiter arb(32);
  mf::phase2::Pipeline piped(/*gap_window=*/32, /*per_venue_capacity=*/1024);
  for (const auto& item : raced) {
    (void)arb.on_event(item.first, item.second);
    auto ready = arb.drain_ready();
    for (const auto& ev : ready) {
      piped.on_event(ev);
    }
  }
  piped.finalize();
  assert(piped.stats().merged_crc == crc_ref);
}

}  // namespace

int main() {
  test_first_wins_duplicate_dropped();
  test_drop_injection_and_pipeline_crc_non_zero();
  test_complementary_ab_drops_preserve_crc();
  return 0;
}
