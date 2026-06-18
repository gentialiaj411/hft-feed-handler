#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

#include "mf/research/alpha_lab/deflated_sharpe.hpp"

int main() {
  const std::vector<double> positive = {0.01, 0.02, 0.015, 0.018, 0.012, 0.017, 0.014, 0.016};
  const double sr = mf::research::alpha_lab::sharpe_ratio(positive);
  assert(sr > 0.0);

  mf::research::alpha_lab::DeflatedSharpeInput in{};
  in.observed_sharpe = sr;
  in.n_trials = 12;
  in.n_observations = positive.size();
  in.skewness = mf::research::alpha_lab::skewness(positive);
  in.kurtosis = 3.0 + mf::research::alpha_lab::kurtosis_excess(positive);
  in.sharpe_std_error = 1.0 / std::sqrt(static_cast<double>(positive.size()));
  const auto dsr = mf::research::alpha_lab::compute_deflated_sharpe(in);
  assert(std::isfinite(dsr.deflated_sharpe));
  assert(dsr.p_value >= 0.0 && dsr.p_value <= 1.0);

  const std::vector<double> flat(20, 0.0);
  const double flat_sr = mf::research::alpha_lab::sharpe_ratio(flat);
  assert(std::fabs(flat_sr) < 1e-9);
  return 0;
}
