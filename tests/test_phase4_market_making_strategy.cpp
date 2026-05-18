#include <cassert>
#include <cstdint>
#include <vector>

#include "mf/phase4/strategy.hpp"

namespace {

struct RoutedOrder {
  std::uint64_t symbol{0};
  mf::phase4::OrderSide side{mf::phase4::OrderSide::Buy};
  std::int64_t price{0};
  std::uint64_t qty{0};
  std::uint64_t order_id{0};
};

class TestRouter final : public mf::phase4::IOrderRouter {
 public:
  void submit(std::uint64_t symbol_u64, mf::phase4::OrderSide side, std::int64_t price, std::uint64_t qty, std::uint64_t, std::uint64_t order_id) override {
    submits.push_back({symbol_u64, side, price, qty, order_id});
  }
  void cancel(std::uint64_t order_id, std::uint64_t) override { cancels.push_back(order_id); }
  std::vector<RoutedOrder> submits{};
  std::vector<std::uint64_t> cancels{};
};

mf::phase3::FeatureVector fv(std::uint64_t sym, double micro, double ofi, std::uint64_t ts) {
  mf::phase3::FeatureVector f{};
  f.symbol_u64 = sym;
  f.microprice = micro;
  f.ofi = ofi;
  f.exchange_ts_ns = ts;
  return f;
}

void test_symmetric_quotes() {
  TestRouter r;
  mf::phase4::MarketMakingStrategy s(&r, {.half_spread_ticks = 2, .quote_size = 10, .max_inventory = 100, .inventory_skew_ticks_per_unit = 0.0, .ofi_skew_coef = 0.0, .requote_threshold_ticks = 1, .cancel_replace_cooldown_ns = 0});
  s.on_feature(fv(1, 100.0, 0.0, 1));
  assert(r.submits.size() == 2);
  assert(r.submits[0].price == 98);
  assert(r.submits[1].price == 102);
}

void test_bid_skew_with_positive_inventory() {
  TestRouter r;
  mf::phase4::MarketMakingStrategy s(&r, {.half_spread_ticks = 1, .quote_size = 10, .max_inventory = 100, .inventory_skew_ticks_per_unit = 1.0, .ofi_skew_coef = 0.0, .requote_threshold_ticks = 1, .cancel_replace_cooldown_ns = 0});
  s.on_feature(fv(1, 100.0, 0.0, 1));
  const auto bid_id = r.submits[0].order_id;
  s.on_fill({bid_id, mf::phase4::OrderSide::Buy, 99, 5, 2, true});
  s.on_feature(fv(1, 100.0, 0.0, 3));
  assert(r.submits.size() >= 4);
  assert(r.submits[2].price < 99);
}

void test_requote_threshold() {
  TestRouter r;
  mf::phase4::MarketMakingStrategy s(&r, {.half_spread_ticks = 1, .quote_size = 10, .max_inventory = 100, .inventory_skew_ticks_per_unit = 0.0, .ofi_skew_coef = 0.0, .requote_threshold_ticks = 3, .cancel_replace_cooldown_ns = 0});
  s.on_feature(fv(1, 100.0, 0.0, 1));
  s.on_feature(fv(1, 101.0, 0.0, 2));
  assert(r.submits.size() == 2);
  s.on_feature(fv(1, 104.0, 0.0, 3));
  assert(r.cancels.size() == 2);
  assert(r.submits.size() == 4);
}

void test_no_double_active_quote() {
  TestRouter r;
  mf::phase4::MarketMakingStrategy s(&r, {.half_spread_ticks = 1, .quote_size = 10, .max_inventory = 100, .inventory_skew_ticks_per_unit = 0.0, .ofi_skew_coef = 0.0, .requote_threshold_ticks = 1, .cancel_replace_cooldown_ns = 0});
  s.on_feature(fv(1, 100.0, 0.0, 1));
  s.on_feature(fv(1, 102.0, 0.0, 2));
  assert(r.cancels.size() == 2);
  assert(r.submits.size() == 4);
}

}  // namespace

int main() {
  test_symmetric_quotes();
  test_bid_skew_with_positive_inventory();
  test_requote_threshold();
  test_no_double_active_quote();
  return 0;
}
