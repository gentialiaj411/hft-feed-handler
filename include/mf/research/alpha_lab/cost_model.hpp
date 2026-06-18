#pragma once

#include <cstddef>
#include <vector>

namespace mf::research::alpha_lab {

struct CostModelConfig {
  double half_spread_bps{5.0};
  double impact_lambda{1e-6};
  double default_spread{0.01};
};

class CostModel {
 public:
  CostModel();
  explicit CostModel(CostModelConfig cfg);

  [[nodiscard]] double crossing_cost(double spread, double quantity) const;
  [[nodiscard]] double impact_cost(double quantity) const;
  [[nodiscard]] double net_return(double gross_return, double spread, double quantity) const;

  [[nodiscard]] double calibrate_lambda_from_spreads(const std::vector<double>& spreads) const;

 private:
  CostModelConfig cfg_{};
};

}  // namespace mf::research::alpha_lab
