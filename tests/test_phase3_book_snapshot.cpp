#include <cassert>
#include <cstdint>

#include "mf/core/types.hpp"
#include "mf/phase3/order_book_engine.hpp"

namespace {

mf::core::BookEvent make_event(
    mf::core::EventType type,
    std::uint64_t seq,
    mf::core::Side side,
    std::uint32_t price,
    std::uint32_t qty,
    std::uint64_t order_id,
    std::uint64_t ref_id = 0) {
  mf::core::BookEvent ev{};
  ev.venue = mf::core::Venue::Nasdaq;
  ev.type = type;
  ev.sequence = seq;
  ev.exchange_ts_ns = seq;
  ev.symbol.bytes = {'A', 'A', 'P', 'L', '0', '0', '0', '1'};
  ev.side = side;
  ev.price = price;
  ev.qty = qty;
  ev.order_id = order_id;
  ev.reference_order_id = ref_id;
  return ev;
}

void test_full_depth_snapshot_is_additive_to_top_of_book() {
  mf::phase3::OrderBookEngine engine{};
  const auto symbol = mf::core::SymbolKey{{'A', 'A', 'P', 'L', '0', '0', '0', '1'}}.as_u64();

  engine.on_event(make_event(mf::core::EventType::Add, 1, mf::core::Side::Buy, 10000, 100, 1));
  engine.on_event(make_event(mf::core::EventType::Add, 2, mf::core::Side::Buy, 9990, 200, 2));
  engine.on_event(make_event(mf::core::EventType::Add, 3, mf::core::Side::Sell, 10020, 150, 3));
  engine.on_event(make_event(mf::core::EventType::Add, 4, mf::core::Side::Sell, 10030, 250, 4));

  const auto top = engine.top_of_book(mf::core::Venue::Nasdaq, symbol);
  assert(top.has_bid);
  assert(top.bid_price == 10000);
  assert(top.bid_qty == 100);
  assert(top.has_ask);
  assert(top.ask_price == 10020);
  assert(top.ask_qty == 150);

  const auto snap = engine.snapshot(mf::core::Venue::Nasdaq, symbol);
  assert(snap.bids.size() == 2);
  assert(snap.bids[0].price == 10000);
  assert(snap.bids[0].qty == 100);
  assert(snap.bids[1].price == 9990);
  assert(snap.bids[1].qty == 200);
  assert(snap.asks.size() == 2);
  assert(snap.asks[0].price == 10020);
  assert(snap.asks[0].qty == 150);
  assert(snap.asks[1].price == 10030);
  assert(snap.asks[1].qty == 250);
}

void test_snapshot_tracks_aggregate_updates() {
  mf::phase3::OrderBookEngine engine{};
  const auto symbol = mf::core::SymbolKey{{'A', 'A', 'P', 'L', '0', '0', '0', '1'}}.as_u64();

  engine.on_event(make_event(mf::core::EventType::Add, 1, mf::core::Side::Buy, 10000, 100, 1));
  engine.on_event(make_event(mf::core::EventType::Add, 2, mf::core::Side::Buy, 10000, 75, 2));
  engine.on_event(make_event(mf::core::EventType::Execute, 3, mf::core::Side::Unknown, 0, 25, 1));
  engine.on_event(make_event(mf::core::EventType::Replace, 4, mf::core::Side::Buy, 10010, 50, 5, 2));

  const auto snap = engine.snapshot(mf::core::Venue::Nasdaq, symbol);
  assert(snap.bids.size() == 2);
  assert(snap.bids[0].price == 10010);
  assert(snap.bids[0].qty == 50);
  assert(snap.bids[1].price == 10000);
  assert(snap.bids[1].qty == 75);
}

}  // namespace

int main() {
  test_full_depth_snapshot_is_additive_to_top_of_book();
  test_snapshot_tracks_aggregate_updates();
  return 0;
}
