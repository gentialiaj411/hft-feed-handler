#include "mf/research/metrics/risk_metrics.hpp"

#include <cmath>

namespace mf::research {

RiskMetrics compute_risk_metrics(const RiskMetricsInput& in) {
  RiskMetrics out{};

  if (!in.returns.empty()) {
    double sum = 0.0;
    double sum_sq = 0.0;
    double downside_sq = 0.0;
    std::uint64_t downside_n = 0;
    for (const double r : in.returns) {
      sum += r;
      sum_sq += r * r;
      if (r < 0.0) {
        downside_sq += r * r;
        ++downside_n;
      }
    }
    const double n = static_cast<double>(in.returns.size());
    const double mean = sum / n;
    const double var = (sum_sq / n) - (mean * mean);
    if (var > 0.0) {
      out.sharpe_ratio = mean / std::sqrt(var);
    }
    if (downside_n > 0) {
      const double downside_dev = std::sqrt(downside_sq / static_cast<double>(downside_n));
      if (downside_dev > 0.0) {
        out.sortino_ratio = mean / downside_dev;
      }
    }
  }

  if (!in.equity_curve.empty()) {
    double peak = in.equity_curve.front();
    for (const double v : in.equity_curve) {
      if (v > peak) peak = v;
      const double dd = peak - v;
      if (dd > out.max_drawdown) out.max_drawdown = dd;
    }
  }

  if (!in.trade_pnls.empty()) {
    std::uint64_t wins = 0;
    double sum = 0.0;
    for (const double p : in.trade_pnls) {
      if (p > 0.0) ++wins;
      sum += p;
    }
    out.win_rate = static_cast<double>(wins) / static_cast<double>(in.trade_pnls.size());
    out.avg_pnl_per_trade = sum / static_cast<double>(in.trade_pnls.size());
  }

  if (in.orders_submitted > 0) {
    out.fill_ratio = static_cast<double>(in.fills) / static_cast<double>(in.orders_submitted);
  }
  out.inventory_turnover = in.inventory_turnover;
  out.avg_holding_time_ms = in.avg_holding_time_ms;
  return out;
}

}  // namespace mf::research
