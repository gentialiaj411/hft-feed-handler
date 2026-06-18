#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

#include "mf/research/alpha_lab/label_builder.hpp"
#include "mf/research/alpha_lab/types.hpp"

namespace {

constexpr double kTol = 1e-12;

mf::research::alpha_lab::LabeledFeatureRow make_row(
    std::uint64_t symbol,
    std::uint64_t ts_ns,
    double mid) {
  mf::research::alpha_lab::LabeledFeatureRow row{};
  row.symbol_u64 = symbol;
  row.exchange_ts_ns = ts_ns;
  row.mid = mid;
  row.forward_mid_returns = {mf::research::alpha_lab::kMissingLabel,
                             mf::research::alpha_lab::kMissingLabel,
                             mf::research::alpha_lab::kMissingLabel};
  return row;
}

void assert_close(double got, double expected) {
  assert(std::isfinite(got));
  assert(std::fabs(got - expected) <= kTol);
}

void test_forward_mid_return_known_move() {
  std::vector<mf::research::alpha_lab::LabeledFeatureRow> rows;
  rows.push_back(make_row(1, 0ULL, 100.0));
  rows.push_back(make_row(1, 500'000'000ULL, 101.0));
  rows.push_back(make_row(1, 1'000'000'000ULL, 103.0));
  rows.push_back(make_row(1, 5'000'000'000ULL, 110.0));
  rows.push_back(make_row(1, 10'000'000'000ULL, 120.0));

  mf::research::alpha_lab::LabelBuilder builder{};
  builder.attach_labels(rows);

  assert_close(rows[0].forward_mid_returns[0], 0.03);
  assert_close(rows[0].forward_mid_returns[1], 0.10);
  assert_close(rows[0].forward_mid_returns[2], 0.20);

  const auto timeline = mf::research::alpha_lab::LabelBuilder::build_timeline(rows);
  assert_close(
      mf::research::alpha_lab::LabelBuilder::forward_mid_return(timeline, 0, 1'000'000'000ULL),
      0.03);
}

void test_missing_label_when_future_unavailable() {
  std::vector<mf::research::alpha_lab::LabeledFeatureRow> rows;
  rows.push_back(make_row(1, 9'500'000'000ULL, 100.0));
  rows.push_back(make_row(1, 10'000'000'000ULL, 101.0));

  mf::research::alpha_lab::LabelBuilder builder{};
  builder.attach_labels(rows);

  assert(std::isnan(rows[1].forward_mid_returns[0]));
  assert(std::isnan(rows[1].forward_mid_returns[1]));
  assert(std::isnan(rows[1].forward_mid_returns[2]));
}

}  // namespace

int main() {
  test_forward_mid_return_known_move();
  test_missing_label_when_future_unavailable();
  return 0;
}
