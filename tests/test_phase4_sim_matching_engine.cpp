#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/phase4/sim_matching_engine.hpp"

namespace {

mf::core::BookEvent trade(std::uint64_t sym, mf::core::Side side, std::uint32_t px, std::uint32_t qty, std::uint64_t ts = 1) {
  mf::core::BookEvent ev{};
  ev.type = mf::core::EventType::Trade;
  ev.side = side;
  ev.price = px;
  ev.qty = qty;
  ev.exchange_ts_ns = ts;
  for (int i = 0; i < 8; ++i) {
    ev.symbol.bytes[i] = static_cast<char>((sym >> ((7 - i) * 8)) & 0xFF);
  }
  return ev;
}

void test_resting_bid_fills_on_sell_print() {
  mf::phase4::SimMatchingEngine eng(0.25);
  std::vector<mf::phase4::Fill> fills;
  eng.set_fill_callback([&](const mf::phase4::Fill& f) { fills.push_back(f); });
  eng.bind_order_symbol(1, 0x4141414141414141ULL);
  eng.submit(mf::phase4::OrderSide::Buy, 100, 50, 1, 1);
  eng.on_market_event(trade(0x4141414141414141ULL, mf::core::Side::Sell, 100, 100));
  assert(fills.size() == 1);
  assert(fills[0].qty == 25);
}

void test_price_time_priority_partial() {
  mf::phase4::SimMatchingEngine eng(1.0);
  std::vector<mf::phase4::Fill> fills;
  eng.set_fill_callback([&](const mf::phase4::Fill& f) { fills.push_back(f); });
  const auto sym = 0x4242424242424242ULL;
  eng.bind_order_symbol(1, sym);
  eng.bind_order_symbol(2, sym);
  eng.submit(mf::phase4::OrderSide::Buy, 101, 20, 1, 1);
  eng.submit(mf::phase4::OrderSide::Buy, 100, 20, 2, 2);
  eng.on_market_event(trade(sym, mf::core::Side::Sell, 100, 30));
  assert(fills.size() == 2);
  assert(fills[0].order_id == 1 && fills[0].qty == 20);
  assert(fills[1].order_id == 2 && fills[1].qty == 10 && fills[1].partial);
}

void test_cancel_removes_order() {
  mf::phase4::SimMatchingEngine eng(1.0);
  std::vector<mf::phase4::Fill> fills;
  eng.set_fill_callback([&](const mf::phase4::Fill& f) { fills.push_back(f); });
  const auto sym = 0x4343434343434343ULL;
  eng.bind_order_symbol(1, sym);
  eng.submit(mf::phase4::OrderSide::Buy, 100, 10, 1, 1);
  eng.cancel(1, 2);
  eng.on_market_event(trade(sym, mf::core::Side::Sell, 100, 10));
  assert(fills.empty());
}

void test_participation_cap() {
  mf::phase4::SimMatchingEngine eng(0.25);
  std::vector<mf::phase4::Fill> fills;
  eng.set_fill_callback([&](const mf::phase4::Fill& f) { fills.push_back(f); });
  const auto sym = 0x4444444444444444ULL;
  eng.bind_order_symbol(1, sym);
  eng.submit(mf::phase4::OrderSide::Sell, 100, 100, 1, 1);
  eng.on_market_event(trade(sym, mf::core::Side::Buy, 100, 40));
  assert(fills.size() == 1);
  assert(fills[0].qty == 10);
}

}  // namespace

int main() {
  test_resting_bid_fills_on_sell_print();
  test_price_time_priority_partial();
  test_cancel_removes_order();
  test_participation_cap();
  return 0;
}
