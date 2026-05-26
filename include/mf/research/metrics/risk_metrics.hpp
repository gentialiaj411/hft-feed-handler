#pragma once

#include <cstdint>
#include <vector>

namespace mf::research {

struct RiskMetricsInput {
  std::vector<double> returns{};
  std::vector<double> equity_curve{};
  std::vector<double> trade_pnls{};
  std::uint64_t orders_submitted{0};
  std::uint64_t fills{0};
  double inventory_turnover{0.0};
  double avg_holding_time_ms{0.0};
};

struct RiskMetrics {
  double sharpe_ratio{0.0};
  double sortino_ratio{0.0};
  double max_drawdown{0.0};
  double win_rate{0.0};
  double avg_pnl_per_trade{0.0};
  double fill_ratio{0.0};
  double inventory_turnover{0.0};
  double avg_holding_time_ms{0.0};
};

RiskMetrics compute_risk_metrics(const RiskMetricsInput& in);

}  // namespace mf::research
