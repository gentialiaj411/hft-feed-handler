#include <cassert>
#include <cstdint>

#include "mf/core/spsc_ring.hpp"
#include "mf/core/types.hpp"
#include "mf/phase2/jit_bridge.hpp"
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

void test_ring_publish_and_drop() {
  mf::core::SPSCRingBuffer<mf::phase2::JitSignalEvent, 2> ring;
  mf::phase2::RingJitPublisher<2> pub(&ring);
  mf::phase2::JitBridge bridge(&pub);

  bridge.on_merged_event(make_event(mf::core::Venue::Nasdaq, 1, 1000));
  bridge.on_merged_event(make_event(mf::core::Venue::Nasdaq, 2, 1001));
  const auto& s = bridge.stats();

  // Capacity 2 ring stores at most 1 item in this implementation.
  assert(s.published == 1);
  assert(s.dropped == 1);
}

void test_pipeline_finalize_to_bridge() {
  mf::phase2::Pipeline pipeline(/*gap_window=*/8, /*per_venue_capacity=*/64);
  mf::core::SPSCRingBuffer<mf::phase2::JitSignalEvent, 64> ring;
  mf::phase2::RingJitPublisher<64> pub(&ring);
  mf::phase2::JitBridge bridge(&pub);

  pipeline.on_event(make_event(mf::core::Venue::Nasdaq, 1, 1000));
  pipeline.on_event(make_event(mf::core::Venue::Nasdaq, 2, 1001));
  pipeline.finalize(&bridge);

  const auto& p = pipeline.stats();
  const auto& j = bridge.stats();
  assert(p.accepted == 2);
  assert(j.published == 2);
  assert(j.dropped == 0);
}

}  // namespace

int main() {
  test_ring_publish_and_drop();
  test_pipeline_finalize_to_bridge();
  return 0;
}
