#include <cassert>

#include "mf/research/metrics/risk_metrics.hpp"

int main() {
  mf::research::RiskMetricsInput in{};
  in.returns = {1.0, -0.5, 0.5, -0.25};
  in.equity_curve = {0.0, 1.0, 0.5, 1.5, 1.0};
  in.trade_pnls = {2.0, -1.0, 1.0};
  in.orders_submitted = 10;
  in.fills = 4;
  in.inventory_turnover = 100.0;
  in.avg_holding_time_ms = 5.0;
  const auto m = mf::research::compute_risk_metrics(in);
  assert(m.sharpe_ratio != 0.0);
  assert(m.sortino_ratio != 0.0);
  assert(m.max_drawdown == 0.5);
  assert(m.win_rate == (2.0 / 3.0));
  assert(m.avg_pnl_per_trade == (2.0 / 3.0));
  assert(m.fill_ratio == 0.4);
  return 0;
}
