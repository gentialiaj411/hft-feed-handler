#include <cassert>
#include <cstdint>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/phase2/ab_arbiter.hpp"
#include "mf/phase2/pipeline.hpp"

namespace {
mf::core::BookEvent ev(std::uint64_t s) {
  mf::core::BookEvent e{}; e.venue = mf::core::Venue::Nasdaq; e.sequence = s; e.exchange_ts_ns = s; e.ingest_ts_ns = s + 1;
  e.type = mf::core::EventType::Add; e.side = mf::core::Side::Buy; e.qty = 10; e.price = 10000 + static_cast<std::uint32_t>(s); e.order_id = s; e.raw_type = 'A';
  e.symbol.bytes = {'A','A','P','L',' ',' ',' ',' '};
  return e;
}
}

int main() {
  std::vector<mf::core::BookEvent> src;
  for (std::uint64_t i = 1; i <= 20000; ++i) src.push_back(ev(i));
  mf::phase2::Pipeline base(256, 1U << 16U);
  for (const auto& e : src) base.on_event(e);
  base.finalize();
  const std::uint32_t crc_ref = base.stats().merged_crc;

  mf::phase2::AbArbiter arb(256);
  mf::phase2::Pipeline p(256, 1U << 16U);
  for (const auto& e : src) {
    if ((e.sequence & 1ULL) == 0ULL) {
      (void)arb.on_event(mf::phase2::FeedSide::B, e);
    } else {
      if ((e.sequence % 4ULL) != 0ULL) (void)arb.on_event(mf::phase2::FeedSide::A, e); // ~50% keep
      (void)arb.on_event(mf::phase2::FeedSide::B, e);
    }
    auto ready = arb.drain_ready();
    for (const auto& r : ready) p.on_event(r);
  }
  p.finalize();
  assert(p.stats().merged_crc == crc_ref);
  assert(arb.stats().forced_advances == 0);
  return 0;
}
