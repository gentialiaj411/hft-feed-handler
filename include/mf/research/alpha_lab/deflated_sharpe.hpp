#pragma once

#include <cstdint>
#include <vector>

namespace mf::research::alpha_lab {

struct DeflatedSharpeInput {
  double observed_sharpe{0.0};
  std::uint64_t n_trials{1};
  std::uint64_t n_observations{0};
  double skewness{0.0};
  double kurtosis{3.0};
  double sharpe_std_error{1.0};
};

struct DeflatedSharpeResult {
  double deflated_sharpe{0.0};
  double p_value{1.0};
  double expected_max_sharpe{0.0};
};

[[nodiscard]] DeflatedSharpeResult compute_deflated_sharpe(const DeflatedSharpeInput& in);

[[nodiscard]] double sharpe_ratio(const std::vector<double>& returns);
[[nodiscard]] double skewness(const std::vector<double>& values);
[[nodiscard]] double kurtosis_excess(const std::vector<double>& values);

}  // namespace mf::research::alpha_lab
