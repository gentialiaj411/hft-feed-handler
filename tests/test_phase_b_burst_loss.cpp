#include <cassert>
#include <cstdint>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/phase2/ab_arbiter.hpp"
#include "mf/phase2/pipeline.hpp"

namespace {
mf::core::BookEvent ev(std::uint64_t s) {
  mf::core::BookEvent e{}; e.venue = mf::core::Venue::Iex; e.sequence = s; e.exchange_ts_ns = s; e.ingest_ts_ns = s + 1;
  e.type = mf::core::EventType::Add; e.side = mf::core::Side::Sell; e.qty = 7; e.price = 12000 + static_cast<std::uint32_t>(s); e.order_id = s; e.raw_type = 'a';
  e.symbol.bytes = {'M','S','F','T',' ',' ',' ',' '};
  return e;
}
}

int main() {
  std::vector<mf::core::BookEvent> src;
  for (std::uint64_t i = 1; i <= 5000; ++i) src.push_back(ev(i));
  mf::phase2::Pipeline base(256, 1U << 15U);
  for (const auto& e : src) base.on_event(e);
  base.finalize();
  const std::uint32_t crc_ref = base.stats().merged_crc;

  mf::phase2::AbArbiter arb(256);
  mf::phase2::Pipeline p(256, 1U << 15U);
  for (const auto& e : src) {
    const bool drop_burst_a = (e.sequence >= 1000 && e.sequence < 1100);
    if (!drop_burst_a) (void)arb.on_event(mf::phase2::FeedSide::A, e);
    (void)arb.on_event(mf::phase2::FeedSide::B, e);
    auto ready = arb.drain_ready();
    for (const auto& r : ready) p.on_event(r);
  }
  p.finalize();
  assert(p.stats().merged_crc == crc_ref);
  return 0;
}
