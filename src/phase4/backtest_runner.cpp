#include "mf/phase4/backtest_runner.hpp"

namespace mf::phase4 {

void BacktestRunner::EngineRouter::submit(
    std::uint64_t symbol_u64,
    OrderSide side,
    std::int64_t price,
    std::uint64_t qty,
    std::uint64_t ts_ns,
    std::uint64_t order_id) {
  if (engine_ == nullptr || pnl_ == nullptr || order_to_symbol_ == nullptr) {
    return;
  }
  (*order_to_symbol_)[order_id] = symbol_u64;
  pnl_->on_order_submitted();
  engine_->bind_order_symbol(order_id, symbol_u64);
  engine_->submit(side, price, qty, order_id, ts_ns);
}

void BacktestRunner::EngineRouter::cancel(std::uint64_t order_id, std::uint64_t ts_ns) {
  if (engine_ == nullptr || order_to_symbol_ == nullptr) {
    return;
  }
  engine_->cancel(order_id, ts_ns);
  order_to_symbol_->erase(order_id);
}

bool BacktestRunner::StrategyPublisher::try_publish(const mf::phase3::FeatureVector& fv) noexcept {
  if (pnl_ != nullptr) {
    pnl_->on_feature(fv);
  }
  if (strategy_ != nullptr) {
    strategy_->on_feature(fv);
  }
  return true;
}

BacktestRunner::BacktestRunner()
    : BacktestRunner(MarketMakingStrategy::Config{}, PnlAccountant::Config{}, 0.25) {}

BacktestRunner::BacktestRunner(
    MarketMakingStrategy::Config strategy_cfg,
    PnlAccountant::Config pnl_cfg,
    double participation_cap)
    : engine_(participation_cap),
      pnl_(pnl_cfg),
      order_to_symbol_(),
      router_(&engine_, &pnl_, &order_to_symbol_),
      strategy_(&router_, strategy_cfg),
      publisher_(&strategy_, &pnl_),
      bridge_(&publisher_) {
  engine_.set_fill_callback([this](const Fill& fill) {
    const auto it = order_to_symbol_.find(fill.order_id);
    if (it == order_to_symbol_.end()) {
      return;
    }
    pnl_.on_fill(it->second, fill);
    strategy_.on_fill(fill);
    if (!fill.partial) {
      order_to_symbol_.erase(it);
    }
  });
}

BacktestReport BacktestRunner::run(std::span<const mf::core::BookEvent> events) {
  for (const auto& ev : events) {
    bridge_.on_merged_event(ev);
    engine_.on_market_event(ev);
    strategy_.on_tick_end(ev.exchange_ts_ns);
    pnl_.on_tick_end(ev.exchange_ts_ns);
  }
  const auto report = pnl_.finalize();
  return BacktestReport{
      report.total_realized_pnl,
      report.total_unrealized_pnl,
      report.total_realized_pnl + report.total_unrealized_pnl,
      report.sharpe,
      report.fill_ratio,
      report.max_drawdown,
      report.turnover,
      report.fills,
      report.submitted_orders};
}

}  // namespace mf::phase4
