#pragma once

#include <cstdint>
#include <span>
#include <unordered_map>

#include "mf/core/types.hpp"
#include "mf/phase2/pipeline.hpp"
#include "mf/phase3/feature_bridge.hpp"
#include "mf/phase4/pnl_accountant.hpp"
#include "mf/phase4/sim_matching_engine.hpp"
#include "mf/phase4/strategy.hpp"

namespace mf::phase4 {

struct BacktestReport {
  double realized_pnl{0.0};
  double unrealized_pnl{0.0};
  double total_pnl{0.0};
  double sharpe{0.0};
  double fill_ratio{0.0};
  double max_drawdown{0.0};
  double turnover{0.0};
  std::uint64_t fills{0};
  std::uint64_t submitted_orders{0};
};

class BacktestRunner {
 public:
  BacktestRunner();
  BacktestRunner(MarketMakingStrategy::Config strategy_cfg,
                 PnlAccountant::Config pnl_cfg,
                 double participation_cap);

  BacktestReport run(std::span<const mf::core::BookEvent> events);

 private:
  class EngineRouter final : public IOrderRouter {
   public:
    EngineRouter(SimMatchingEngine* engine, PnlAccountant* pnl, std::unordered_map<std::uint64_t, std::uint64_t>* order_to_symbol)
        : engine_(engine), pnl_(pnl), order_to_symbol_(order_to_symbol) {}

    void submit(std::uint64_t symbol_u64, OrderSide side, std::int64_t price, std::uint64_t qty, std::uint64_t ts_ns, std::uint64_t order_id) override;
    void cancel(std::uint64_t order_id, std::uint64_t ts_ns) override;

   private:
    SimMatchingEngine* engine_{nullptr};
    PnlAccountant* pnl_{nullptr};
    std::unordered_map<std::uint64_t, std::uint64_t>* order_to_symbol_{nullptr};
  };

  class StrategyPublisher final : public mf::phase3::IFeaturePublisher {
   public:
    explicit StrategyPublisher(MarketMakingStrategy* strategy, PnlAccountant* pnl) : strategy_(strategy), pnl_(pnl) {}
    bool try_publish(const mf::phase3::FeatureVector& fv) noexcept override;

   private:
    MarketMakingStrategy* strategy_{nullptr};
    PnlAccountant* pnl_{nullptr};
  };

  SimMatchingEngine engine_{};
  PnlAccountant pnl_{};
  std::unordered_map<std::uint64_t, std::uint64_t> order_to_symbol_{};
  EngineRouter router_;
  MarketMakingStrategy strategy_;
  StrategyPublisher publisher_;
  mf::phase3::FeatureBridge bridge_;
};

}  // namespace mf::phase4
