#include "mf/research/alpha_lab/deflated_sharpe.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace mf::research::alpha_lab {

namespace {

constexpr double kPi = 3.14159265358979323846;

double normal_cdf(double x) {
  return 0.5 * (1.0 + std::erf(x / std::sqrt(2.0)));
}

double euler_mascheroni() {
  return 0.5772156649015329;
}

double expected_max_sharpe(std::uint64_t n_trials, double variance) {
  if (n_trials <= 1) {
    return 0.0;
  }
  const double n = static_cast<double>(n_trials);
  const double z1 = std::sqrt(variance);
  const double gamma = euler_mascheroni();
  return z1 * ((1.0 - gamma) * std::sqrt(2.0 * std::log(n)) + gamma / std::sqrt(2.0 * std::log(n)));
}

}  // namespace

double sharpe_ratio(const std::vector<double>& returns) {
  if (returns.size() < 2) {
    return 0.0;
  }
  const double mean = std::accumulate(returns.begin(), returns.end(), 0.0) /
                      static_cast<double>(returns.size());
  double var = 0.0;
  for (double r : returns) {
    const double d = r - mean;
    var += d * d;
  }
  var /= static_cast<double>(returns.size());
  if (var <= 0.0) {
    return 0.0;
  }
  return mean / std::sqrt(var);
}

double skewness(const std::vector<double>& values) {
  if (values.size() < 3) {
    return 0.0;
  }
  const double mean = std::accumulate(values.begin(), values.end(), 0.0) /
                      static_cast<double>(values.size());
  double m2 = 0.0;
  double m3 = 0.0;
  for (double v : values) {
    const double d = v - mean;
    m2 += d * d;
    m3 += d * d * d;
  }
  m2 /= static_cast<double>(values.size());
  m3 /= static_cast<double>(values.size());
  if (m2 <= 0.0) {
    return 0.0;
  }
  return m3 / std::pow(m2, 1.5);
}

double kurtosis_excess(const std::vector<double>& values) {
  if (values.size() < 4) {
    return 0.0;
  }
  const double mean = std::accumulate(values.begin(), values.end(), 0.0) /
                      static_cast<double>(values.size());
  double m2 = 0.0;
  double m4 = 0.0;
  for (double v : values) {
    const double d = v - mean;
    m2 += d * d;
    m4 += d * d * d * d;
  }
  m2 /= static_cast<double>(values.size());
  m4 /= static_cast<double>(values.size());
  if (m2 <= 0.0) {
    return 0.0;
  }
  return m4 / (m2 * m2) - 3.0;
}

DeflatedSharpeResult compute_deflated_sharpe(const DeflatedSharpeInput& in) {
  DeflatedSharpeResult out{};
  const double n = static_cast<double>(std::max<std::uint64_t>(2, in.n_observations));
  const double sr_std = in.sharpe_std_error > 0.0 ? in.sharpe_std_error : 1.0 / std::sqrt(n);
  const double sr_var = sr_std * sr_std;

  const double adjustment =
      (in.skewness / 6.0) * in.observed_sharpe + ((in.kurtosis - 3.0) / 24.0) * in.observed_sharpe * in.observed_sharpe;
  const double sr_star = (in.observed_sharpe - adjustment) * std::sqrt((n - 1.0) / (n - 2.0));

  out.expected_max_sharpe = expected_max_sharpe(std::max<std::uint64_t>(1, in.n_trials), sr_var);
  const double z = (sr_star - out.expected_max_sharpe) / sr_std;
  out.deflated_sharpe = z;
  out.p_value = 1.0 - normal_cdf(z);
  return out;
}

}  // namespace mf::research::alpha_lab
