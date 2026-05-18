#include <cassert>
#include <cmath>
#include <cstdint>

#include "mf/core/types.hpp"
#include "mf/phase3/feature_pipeline.hpp"
#include "mf/phase3/types.hpp"

namespace {

constexpr double kTol = 1e-10;

mf::core::BookEvent make_event(
    std::uint64_t exchange_ts_ns,
    bool is_trade = false,
    std::uint32_t trade_price = 0,
    double signed_qty = 0.0) {
  mf::core::BookEvent ev{};
  ev.exchange_ts_ns = exchange_ts_ns;
  ev.type = is_trade ? mf::core::EventType::Trade : mf::core::EventType::Unknown;
  ev.price = trade_price;
  ev.qty = static_cast<std::uint32_t>(std::fabs(signed_qty));
  ev.side = signed_qty > 0.0 ? mf::core::Side::Buy : (signed_qty < 0.0 ? mf::core::Side::Sell : mf::core::Side::Unknown);
  return ev;
}

mf::phase3::Nbbo make_nbbo(
    bool has_bid,
    bool has_ask,
    std::uint32_t bid_price,
    std::uint32_t bid_qty,
    std::uint32_t ask_price,
    std::uint32_t ask_qty) {
  mf::phase3::Nbbo nbbo{};
  nbbo.has_bid = has_bid;
  nbbo.has_ask = has_ask;
  nbbo.bid_price = bid_price;
  nbbo.bid_qty = bid_qty;
  nbbo.ask_price = ask_price;
  nbbo.ask_qty = ask_qty;
  return nbbo;
}

void assert_close(double got, double expected) {
  assert(std::fabs(got - expected) <= kTol);
}

void test_microprice() {
  mf::phase3::FeaturePipeline pipeline{};

  const auto nbbo = make_nbbo(true, true, 10000, 200, 10200, 100);
  const auto fv = pipeline.on_event(make_event(1'000'000'000ULL), nbbo, 0.0);
  assert(fv.has_value());
  assert_close(fv->microprice, (10200.0 * 200.0 + 10000.0 * 100.0) / 300.0);

  const auto nbbo_equal = make_nbbo(true, true, 10000, 100, 10200, 100);
  const auto fv_equal = pipeline.on_event(make_event(1'500'000'000ULL), nbbo_equal, 0.0);
  assert(fv_equal.has_value());
  assert_close(fv_equal->microprice, 10100.0);
}

void test_ofi() {
  mf::phase3::FeaturePipeline pipeline{};

  const auto nbbo0 = make_nbbo(true, true, 100, 50, 102, 30);
  const auto fv0 = pipeline.on_event(make_event(0ULL), nbbo0, 0.0);
  assert(fv0.has_value());
  assert_close(fv0->ofi, 0.0);

  const auto nbbo1 = make_nbbo(true, true, 101, 60, 102, 30);
  const auto fv1 = pipeline.on_event(make_event(100'000'000ULL), nbbo1, 0.0);
  assert(fv1.has_value());
  assert_close(fv1->ofi, 60.0);

  const auto nbbo2 = make_nbbo(true, true, 101, 60, 103, 20);
  const auto fv2 = pipeline.on_event(make_event(200'000'000ULL), nbbo2, 0.0);
  assert(fv2.has_value());
  assert_close(fv2->ofi, 90.0);

  const auto fv3 = pipeline.on_event(make_event(1'200'000'000ULL), nbbo2, 0.0);
  assert(fv3.has_value());
  assert_close(fv3->ofi, 30.0);
}

void test_effective_spread() {
  mf::phase3::FeaturePipeline pipeline{};
  const auto nbbo = make_nbbo(true, true, 1000, 100, 1020, 100);

  const auto fv1 = pipeline.on_event(make_event(1'000'000'000ULL, true, 1012, 100.0), nbbo, 0.0);
  assert(fv1.has_value());
  assert_close(fv1->effective_spread, 4.0);

  const auto fv2 = pipeline.on_event(make_event(2'000'000'000ULL, true, 1008, -100.0), nbbo, 0.0);
  assert(fv2.has_value());
  assert_close(fv2->effective_spread, 4.0);

  const auto fv3 = pipeline.on_event(make_event(3'000'000'000ULL, true, 1016, 50.0), nbbo, 0.0);
  assert(fv3.has_value());
  assert_close(fv3->effective_spread, 4.4);
}

void test_kyle_lambda() {
  mf::phase3::FeaturePipeline pipeline{};
  const auto nbbo1 = make_nbbo(true, true, 1000, 100, 1020, 100);

  const auto fv1 = pipeline.on_event(make_event(1'000'000'000ULL, true, 1012, 100.0), nbbo1, 0.0);
  assert(fv1.has_value());

  const auto nbbo2 = make_nbbo(true, true, 1010, 100, 1020, 100);
  const auto fv2 = pipeline.on_event(make_event(2'000'000'000ULL, true, 1016, 200.0), nbbo2, 0.0);
  assert(fv2.has_value());

  const double n = 2.0;
  const double sx = 300.0;
  const double sy = 5.0;
  const double sxx = 50'000.0;
  const double sxy = 1000.0;
  const double expected = (n * sxy - sx * sy) / (n * sxx - sx * sx);
  assert_close(fv2->kyle_lambda, expected);
}

void test_vpin() {
  mf::phase3::FeaturePipeline pipeline{};
  const auto nbbo = make_nbbo(true, true, 1000, 100, 1020, 100);

  const auto fv1 = pipeline.on_event(make_event(1'000'000'000ULL, true, 1010, 6000.0), nbbo, 0.0);
  assert(fv1.has_value());
  const auto fv2 = pipeline.on_event(make_event(2'000'000'000ULL, true, 1010, -4000.0), nbbo, 0.0);
  assert(fv2.has_value());
  assert_close(fv2->vpin, 0.2);

  const auto fv3 = pipeline.on_event(make_event(3'000'000'000ULL, true, 1010, -10000.0), nbbo, 0.0);
  assert(fv3.has_value());
  assert_close(fv3->vpin, 0.6);
}

void test_nbbo_guard() {
  mf::phase3::FeaturePipeline pipeline{};
  const auto bad_nbbo = make_nbbo(false, true, 0, 0, 1020, 100);
  const auto zero_nbbo = make_nbbo(true, true, 0, 0, 0, 0);
  assert(!pipeline.on_event(make_event(1'000'000'000ULL), bad_nbbo, 0.0).has_value());
  assert(!pipeline.on_event(make_event(1'000'000'000ULL), zero_nbbo, 0.0).has_value());
}

}  // namespace

int main() {
  test_microprice();
  test_ofi();
  test_effective_spread();
  test_kyle_lambda();
  test_vpin();
  test_nbbo_guard();
  return 0;
}
