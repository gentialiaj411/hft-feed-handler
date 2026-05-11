#include <cassert>
#include <cstdint>

#include "mf/core/types.hpp"
#include "mf/phase2/pipeline.hpp"

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
  pipeline.on_event(make_event(mf::core::Venue::Iex, 10, 2010));
  pipeline.finalize();
  const auto& s = pipeline.stats();
  assert(s.dropped_gap_too_large == 1);
}

}  // namespace

int main() {
  test_out_of_order_then_recovery_reinject();
  test_gap_too_large_drop();
  return 0;
}
