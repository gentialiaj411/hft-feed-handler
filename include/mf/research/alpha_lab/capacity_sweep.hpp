#pragma once

#include <cstddef>
#include <vector>

#include "mf/research/alpha_lab/cost_model.hpp"

namespace mf::research::alpha_lab {

struct CapacityPoint {
  double participation{0.0};
  double gross_sharpe{0.0};
  double net_sharpe{0.0};
  double mean_return{0.0};
};

struct CapacitySweepConfig {
  std::vector<double> participation_grid{0.01, 0.05, 0.10, 0.25, 0.50, 1.0};
  CostModelConfig costs{};
};

class CapacitySweep {
 public:
  CapacitySweep();
  explicit CapacitySweep(CapacitySweepConfig cfg);

  [[nodiscard]] std::vector<CapacityPoint> sweep(
      const std::vector<double>& signal,
      const std::vector<double>& labels,
      const std::vector<double>& spreads) const;

  [[nodiscard]] double capacity_ceiling(const std::vector<CapacityPoint>& curve) const;

 private:
  CapacitySweepConfig cfg_{};
  CostModel costs_{};
};

}  // namespace mf::research::alpha_lab
