#include "mf/research/alpha_lab/cost_model.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace mf::research::alpha_lab {

CostModel::CostModel() = default;

CostModel::CostModel(CostModelConfig cfg) : cfg_(cfg) {}

double CostModel::crossing_cost(const double spread, const double quantity) const {
  const double half_spread = 0.5 * spread + (spread <= 0.0 ? cfg_.default_spread : 0.0);
  const double bps = cfg_.half_spread_bps * 1e-4;
  return quantity * (half_spread + bps);
}

double CostModel::impact_cost(const double quantity) const {
  return cfg_.impact_lambda * std::fabs(quantity);
}

double CostModel::net_return(
    const double gross_return,
    const double spread,
    const double quantity) const {
  const double cost = crossing_cost(spread, quantity) + impact_cost(quantity);
  return gross_return - cost;
}

double CostModel::calibrate_lambda_from_spreads(const std::vector<double>& spreads) const {
  if (spreads.empty()) {
    return cfg_.impact_lambda;
  }
  double sum = 0.0;
  std::size_t n = 0;
  for (double s : spreads) {
    if (s > 0.0) {
      sum += s;
      ++n;
    }
  }
  if (n == 0) {
    return cfg_.impact_lambda;
  }
  return (sum / static_cast<double>(n)) * 1e-7;
}

}  // namespace mf::research::alpha_lab
