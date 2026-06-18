#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/phase3/feature_bridge.hpp"
#include "mf/phase3/feature_pipeline.hpp"
#include "mf/phase3/types.hpp"
#include "mf/research/alpha_lab/label_builder.hpp"
#include "mf/research/alpha_lab/types.hpp"

namespace {

constexpr double kTol = 1e-10;

mf::core::BookEvent make_add(
    std::uint64_t ts,
    mf::core::Side side,
    std::uint32_t price,
    std::uint32_t qty,
    std::uint64_t order_id) {
  mf::core::BookEvent ev{};
  ev.venue = mf::core::Venue::Nasdaq;
  ev.type = mf::core::EventType::Add;
  ev.side = side;
  ev.price = price;
  ev.qty = qty;
  ev.exchange_ts_ns = ts;
  ev.ingest_ts_ns = ts;
  ev.order_id = order_id;
  ev.symbol.bytes = {'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '};
  return ev;
}

mf::phase3::Nbbo make_nbbo(std::uint32_t bid, std::uint32_t ask) {
  mf::phase3::Nbbo nbbo{};
  nbbo.has_bid = true;
  nbbo.has_ask = true;
  nbbo.bid_price = bid;
  nbbo.bid_qty = 100;
  nbbo.ask_price = ask;
  nbbo.ask_qty = 100;
  return nbbo;
}

class FeatureCollector final : public mf::phase3::IFeaturePublisher {
 public:
  bool try_publish(const mf::phase3::FeatureVector& fv) noexcept override {
    rows_.push_back(mf::research::alpha_lab::from_feature_vector(fv, 3));
    return true;
  }

  std::vector<mf::research::alpha_lab::LabeledFeatureRow> rows_{};
};

void replay_prefix(
    const std::vector<mf::core::BookEvent>& events,
    std::size_t count,
    std::vector<mf::research::alpha_lab::LabeledFeatureRow>& out) {
  FeatureCollector collector{};
  mf::phase3::FeatureBridge bridge(&collector);
  for (std::size_t i = 0; i < count && i < events.size(); ++i) {
    bridge.on_merged_event(events[i]);
  }
  out = collector.rows_;
}

void assert_feature_rows_equal(
    const std::vector<mf::research::alpha_lab::LabeledFeatureRow>& a,
    const std::vector<mf::research::alpha_lab::LabeledFeatureRow>& b) {
  assert(a.size() == b.size());
  for (std::size_t i = 0; i < a.size(); ++i) {
    assert(a[i].exchange_ts_ns == b[i].exchange_ts_ns);
    assert(std::fabs(a[i].ofi - b[i].ofi) <= kTol);
    assert(std::fabs(a[i].microprice - b[i].microprice) <= kTol);
    assert(std::fabs(a[i].mid - b[i].mid) <= kTol);
  }
}

void test_features_do_not_use_future_events() {
  const std::vector<mf::core::BookEvent> events = {
      make_add(0ULL, mf::core::Side::Buy, 10000, 100, 1),
      make_add(100'000'000ULL, mf::core::Side::Sell, 10200, 100, 2),
      make_add(2'000'000'000ULL, mf::core::Side::Buy, 10100, 100, 3),
      make_add(3'000'000'000ULL, mf::core::Side::Sell, 10300, 100, 4),
  };

  std::vector<mf::research::alpha_lab::LabeledFeatureRow> prefix_rows;
  std::vector<mf::research::alpha_lab::LabeledFeatureRow> full_rows;
  replay_prefix(events, 2, prefix_rows);
  replay_prefix(events, events.size(), full_rows);

  assert_feature_rows_equal(prefix_rows, std::vector(full_rows.begin(), full_rows.begin() + prefix_rows.size()));
}

void test_labels_use_only_future_mids() {
  std::vector<mf::research::alpha_lab::LabeledFeatureRow> rows;
  rows.push_back(mf::research::alpha_lab::LabeledFeatureRow{
      .symbol_u64 = 1,
      .exchange_ts_ns = 0ULL,
      .mid = 100.0,
      .forward_mid_returns = {mf::research::alpha_lab::kMissingLabel,
                            mf::research::alpha_lab::kMissingLabel,
                            mf::research::alpha_lab::kMissingLabel},
  });
  rows.push_back(mf::research::alpha_lab::LabeledFeatureRow{
      .symbol_u64 = 1,
      .exchange_ts_ns = 1'000'000'000ULL,
      .mid = 103.0,
      .forward_mid_returns = {mf::research::alpha_lab::kMissingLabel,
                            mf::research::alpha_lab::kMissingLabel,
                            mf::research::alpha_lab::kMissingLabel},
  });
  rows.push_back(mf::research::alpha_lab::LabeledFeatureRow{
      .symbol_u64 = 1,
      .exchange_ts_ns = 500'000'000ULL,
      .mid = 200.0,
      .forward_mid_returns = {mf::research::alpha_lab::kMissingLabel,
                            mf::research::alpha_lab::kMissingLabel,
                            mf::research::alpha_lab::kMissingLabel},
  });

  mf::research::alpha_lab::LabelBuilder builder{};
  builder.attach_labels(rows);

  assert(std::fabs(rows[0].forward_mid_returns[0] - 0.03) <= kTol);

  rows[2].mid = 50.0;
  mf::research::alpha_lab::LabelBuilder leaky_builder{};
  const auto timeline = mf::research::alpha_lab::LabelBuilder::build_timeline(rows);
  const double leaky = mf::research::alpha_lab::LabelBuilder::forward_mid_return(
      timeline,
      0,
      1'000'000'000ULL);
  assert(std::fabs(leaky - (-0.50)) <= kTol);

  rows[2].exchange_ts_ns = 2'000'000'000ULL;
  rows[2].mid = 50.0;
  leaky_builder.attach_labels(rows);
  assert(std::fabs(rows[0].forward_mid_returns[0] - 0.03) <= kTol);
}

void test_leaky_label_logic_is_detectable() {
  std::vector<mf::research::alpha_lab::MidObservation> timeline = {
      {.symbol_u64 = 1, .exchange_ts_ns = 0ULL, .mid = 100.0},
      {.symbol_u64 = 1, .exchange_ts_ns = 500'000'000ULL, .mid = 50.0},
      {.symbol_u64 = 1, .exchange_ts_ns = 1'000'000'000ULL, .mid = 103.0},
  };

  const double correct = mf::research::alpha_lab::LabelBuilder::forward_mid_return(
      timeline, 0, 1'000'000'000ULL);
  assert(std::fabs(correct - 0.03) <= kTol);

  timeline[1].exchange_ts_ns = 0ULL;
  const double leaky = mf::research::alpha_lab::LabelBuilder::forward_mid_return(
      timeline, 0, 1'000'000'000ULL);
  assert(std::fabs(leaky - (-0.50)) <= kTol);
  assert(std::fabs(leaky - correct) > 0.1);
}

}  // namespace

int main() {
  test_features_do_not_use_future_events();
  test_labels_use_only_future_mids();
  test_leaky_label_logic_is_detectable();
  return 0;
}
