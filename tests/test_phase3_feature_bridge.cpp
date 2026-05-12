#include <cassert>
#include <cstdint>

#include "mf/core/spsc_ring.hpp"
#include "mf/core/types.hpp"
#include "mf/phase2/pipeline.hpp"
#include "mf/phase3/feature_bridge.hpp"

namespace {

mf::core::BookEvent make_add(
    mf::core::Venue venue,
    std::uint64_t seq,
    std::uint64_t ts,
    mf::core::Side side,
    std::uint32_t price,
    std::uint32_t qty,
    std::uint64_t order_id,
    std::uint64_t symbol_u64 = 0x4141504c20202020ULL) {
  mf::core::BookEvent ev{};
  ev.venue = venue;
  ev.type = mf::core::EventType::Add;
  ev.sequence = seq;
  ev.exchange_ts_ns = ts;
  ev.ingest_ts_ns = ts + 10U;
  ev.side = side;
  ev.price = price;
  ev.qty = qty;
  ev.order_id = order_id;
  for (int i = 7; i >= 0; --i) {
    ev.symbol.bytes[static_cast<std::size_t>(i)] = static_cast<char>(symbol_u64 & 0xFFU);
    symbol_u64 >>= 8U;
  }
  ev.raw_type = static_cast<std::uint8_t>('A');
  return ev;
}

void test_phase3_feature_publication() {
  mf::phase2::Pipeline pipeline(/*gap_window=*/8, /*per_venue_capacity=*/64);
  mf::core::SPSCRingBuffer<mf::phase3::FeatureVector, 128> ring;
  mf::phase3::RingFeaturePublisher<128> pub(&ring);
  mf::phase3::FeatureBridge bridge(&pub);

  pipeline.on_event(make_add(mf::core::Venue::Nasdaq, 1, 1000, mf::core::Side::Buy, 10000, 200, 10));
  pipeline.on_event(make_add(mf::core::Venue::Nasdaq, 2, 1001, mf::core::Side::Sell, 10010, 300, 11));
  pipeline.on_event(make_add(mf::core::Venue::Iex, 1, 1002, mf::core::Side::Buy, 10002, 150, 20));
  pipeline.on_event(make_add(mf::core::Venue::Iex, 2, 1003, mf::core::Side::Sell, 10012, 150, 21));
  pipeline.finalize(&bridge);

  const auto& s = bridge.stats();
  assert(s.feature_updates > 0);
  assert(s.published == s.feature_updates);
  assert(s.dropped == 0);
}

}  // namespace

int main() {
  test_phase3_feature_publication();
  return 0;
}
