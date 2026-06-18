#include "mf/research/alpha_lab/capacity_sweep.hpp"

#include "mf/research/alpha_lab/deflated_sharpe.hpp"

#include <cmath>
#include <numeric>

namespace mf::research::alpha_lab {

CapacitySweep::CapacitySweep() : costs_(cfg_.costs) {}

CapacitySweep::CapacitySweep(CapacitySweepConfig cfg) : cfg_(cfg), costs_(cfg.costs) {}

std::vector<CapacityPoint> CapacitySweep::sweep(
    const std::vector<double>& signal,
    const std::vector<double>& labels,
    const std::vector<double>& spreads) const {
  std::vector<CapacityPoint> curve;
  curve.reserve(cfg_.participation_grid.size());

  for (double participation : cfg_.participation_grid) {
    std::vector<double> gross_returns;
    std::vector<double> net_returns;
    gross_returns.reserve(signal.size());
    net_returns.reserve(signal.size());

    for (std::size_t i = 0; i < signal.size() && i < labels.size(); ++i) {
      if (std::isnan(signal[i]) || std::isnan(labels[i])) {
        continue;
      }
      const double position = (signal[i] > 0.0 ? 1.0 : (signal[i] < 0.0 ? -1.0 : 0.0)) * participation;
      const double gross = position * labels[i];
      const double spread = (i < spreads.size()) ? spreads[i] : cfg_.costs.default_spread;
      const double net = costs_.net_return(gross, spread, position);
      gross_returns.push_back(gross);
      net_returns.push_back(net);
    }

    CapacityPoint pt{};
    pt.participation = participation;
    pt.gross_sharpe = sharpe_ratio(gross_returns);
    pt.net_sharpe = sharpe_ratio(net_returns);
    pt.mean_return = gross_returns.empty()
                         ? 0.0
                         : std::accumulate(gross_returns.begin(), gross_returns.end(), 0.0) /
                               static_cast<double>(gross_returns.size());
    curve.push_back(pt);
  }
  return curve;
}

double CapacitySweep::capacity_ceiling(const std::vector<CapacityPoint>& curve) const {
  double ceiling = 0.0;
  for (const auto& pt : curve) {
    if (pt.net_sharpe > 0.0) {
      ceiling = std::max(ceiling, pt.participation);
    }
  }
  return ceiling;
}

}  // namespace mf::research::alpha_lab
