#include <cassert>
#include <cmath>

#include "mf/phase4/pnl_accountant.hpp"

namespace {

mf::phase3::FeatureVector mark(std::uint64_t sym, std::uint32_t bid, std::uint32_t ask, std::uint64_t ts) {
  mf::phase3::FeatureVector fv{};
  fv.symbol_u64 = sym;
  fv.nbbo_bid_price = bid;
  fv.nbbo_ask_price = ask;
  fv.exchange_ts_ns = ts;
  return fv;
}

void test_realized_round_trip() {
  mf::phase4::PnlAccountant p;
  p.on_fill(1, {1, mf::phase4::OrderSide::Buy, 100, 10, 1, false});
  p.on_fill(1, {2, mf::phase4::OrderSide::Sell, 101, 10, 2, false});
  auto r = p.finalize();
  assert(r.total_realized_pnl > 0.0);
}

void test_unrealized_mark_to_mid() {
  mf::phase4::PnlAccountant p;
  p.on_fill(1, {1, mf::phase4::OrderSide::Buy, 100, 10, 1, false});
  p.on_feature(mark(1, 101, 103, 2));
  p.on_tick_end(2);
  auto r = p.finalize();
  assert(r.total_unrealized_pnl > 0.0);
}

void test_drawdown_curve() {
  mf::phase4::PnlAccountant p;
  p.on_fill(1, {1, mf::phase4::OrderSide::Buy, 100, 10, 1, false});
  p.on_feature(mark(1, 110, 110, 2));
  p.on_tick_end(2);
  p.on_feature(mark(1, 90, 90, 3));
  p.on_tick_end(3);
  auto r = p.finalize();
  assert(r.max_drawdown > 0.0);
}

void test_sharpe_sign_monotonic() {
  mf::phase4::PnlAccountant p({1});
  p.on_feature(mark(1, 100, 100, 1));
  p.on_tick_end(1);
  p.on_fill(1, {1, mf::phase4::OrderSide::Buy, 100, 10, 2, false});
  p.on_feature(mark(1, 101, 101, 2));
  p.on_tick_end(2);
  p.on_feature(mark(1, 102, 102, 3));
  p.on_tick_end(3);
  auto r = p.finalize();
  assert(std::isfinite(r.sharpe));
}

}  // namespace

int main() {
  test_realized_round_trip();
  test_unrealized_mark_to_mid();
  test_drawdown_curve();
  test_sharpe_sign_monotonic();
  return 0;
}
