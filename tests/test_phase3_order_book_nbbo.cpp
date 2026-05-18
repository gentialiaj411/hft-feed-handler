#include <cassert>
#include <cstdint>

#include "mf/core/types.hpp"
#include "mf/phase3/nbbo_consolidator.hpp"
#include "mf/phase3/order_book_engine.hpp"
#include "mf/phase3/types.hpp"

namespace {

mf::core::BookEvent make_event(
    mf::core::Venue venue,
    mf::core::EventType type,
    std::uint64_t sequence,
    std::uint64_t exchange_ts_ns,
    mf::core::Side side,
    std::uint32_t price,
    std::uint32_t qty,
    std::uint64_t order_id,
    std::uint64_t reference_order_id = 0U) {
  mf::core::BookEvent ev{};
  ev.venue = venue;
  ev.type = type;
  ev.sequence = sequence;
  ev.exchange_ts_ns = exchange_ts_ns;
  ev.side = side;
  ev.price = price;
  ev.qty = qty;
  ev.order_id = order_id;
  ev.reference_order_id = reference_order_id;
  return ev;
}

void test_order_book_lifecycle() {
  mf::phase3::OrderBookEngine engine{};

  const auto add_bid = make_event(mf::core::Venue::Nasdaq, mf::core::EventType::Add, 1, 1000, mf::core::Side::Buy, 10000, 200, 10);
  auto r1 = engine.on_event(add_bid);
  assert(r1.changed_top);
  assert(r1.top_after.has_bid);
  assert(r1.top_after.bid_price == 10000);
  assert(r1.top_after.bid_qty == 200);

  const auto add_ask = make_event(mf::core::Venue::Nasdaq, mf::core::EventType::Add, 2, 1001, mf::core::Side::Sell, 10020, 300, 11);
  auto r2 = engine.on_event(add_ask);
  assert(r2.changed_top);
  assert(r2.top_after.has_ask);
  assert(r2.top_after.ask_price == 10020);
  assert(r2.top_after.ask_qty == 300);

  const auto exec_bid = make_event(mf::core::Venue::Nasdaq, mf::core::EventType::Execute, 3, 1002, mf::core::Side::Unknown, 0, 50, 10);
  auto r3 = engine.on_event(exec_bid);
  assert(r3.changed_top);
  assert(r3.top_after.bid_price == 10000);
  assert(r3.top_after.bid_qty == 150);

  const auto cancel_ask = make_event(mf::core::Venue::Nasdaq, mf::core::EventType::Cancel, 4, 1003, mf::core::Side::Unknown, 0, 0, 11);
  auto r4 = engine.on_event(cancel_ask);
  assert(r4.changed_top);
  assert(!r4.top_after.has_ask);

  const auto replace_bid = make_event(mf::core::Venue::Nasdaq, mf::core::EventType::Replace, 5, 1004, mf::core::Side::Buy, 10020, 120, 12, 10);
  auto r5 = engine.on_event(replace_bid);
  assert(r5.changed_top);
  assert(r5.top_after.has_bid);
  assert(r5.top_after.bid_price == 10020);
  assert(r5.top_after.bid_qty == 120);
}

void test_nbbo_consolidation() {
  mf::phase3::NbboConsolidator consolidator{};
  const std::uint64_t symbol = 0x4141504c20202020ULL;

  auto nbbo1 = consolidator.update_and_current(
      symbol, mf::core::Venue::Nasdaq, mf::phase3::TopOfBook{true, true, 10000, 200, 10030, 150});
  assert(nbbo1.has_bid && nbbo1.has_ask);
  assert(nbbo1.bid_price == 10000);
  assert(nbbo1.ask_price == 10030);

  auto nbbo2 = consolidator.update_and_current(
      symbol, mf::core::Venue::Iex, mf::phase3::TopOfBook{true, true, 10010, 100, 10040, 90});
  assert(nbbo2.bid_price == 10010);
  assert(nbbo2.bid_venue == static_cast<std::uint8_t>(mf::core::Venue::Iex));
  assert(nbbo2.ask_price == 10030);
  assert(nbbo2.ask_venue == static_cast<std::uint8_t>(mf::core::Venue::Nasdaq));

  auto nbbo3 = consolidator.update_and_current(
      symbol, mf::core::Venue::Cboe, mf::phase3::TopOfBook{true, true, 10005, 75, 10005, 80});
  assert(nbbo3.has_bid && nbbo3.has_ask);
  assert(nbbo3.bid_price == 10010);
  assert(nbbo3.ask_price == 10005);
  assert(nbbo3.bid_price >= nbbo3.ask_price);
}

}  // namespace

int main() {
  test_order_book_lifecycle();
  test_nbbo_consolidation();
  return 0;
}
