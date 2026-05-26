#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/phase4/pnl_accountant.hpp"
#include "mf/phase4/sim_matching_engine.hpp"
#include "mf/research/event_store.hpp"
#include "mf/research/metrics/risk_metrics.hpp"
#include "mf/research/strategies/ofi_strategy.hpp"
#include "mf/research/strategy_engine.hpp"

namespace mf::research {

struct ExperimentConfig {
  enum class StrategyKind : std::uint8_t { MarketMaking = 0, Ofi = 1 };
  StrategyKind strategy_kind{StrategyKind::MarketMaking};
  StrategyEngine::Config strategy{};
  OfiStrategy::Config ofi_strategy{};
  mf::phase4::PnlAccountant::Config pnl{};
  double participation_cap{0.25};
};

struct ExperimentReport {
  EventStoreStats input{};
  std::uint64_t events_replayed{0};
  std::uint64_t clock_rejects{0};
  std::uint64_t submitted_orders{0};
  std::uint64_t cancel_intents{0};
  std::uint64_t fills{0};
  std::uint32_t config_hash{0};
  std::uint32_t output_hash{0};
  mf::phase4::PnlAccountant::Report pnl{};
  RiskMetrics risk{};
  std::vector<double> period_pnls{};
  std::vector<double> trade_pnls{};
  std::vector<double> equity_curve{};
};

class ExperimentRunner {
 public:
  ExperimentRunner();
  explicit ExperimentRunner(ExperimentConfig cfg);

  bool run(const EventStore& store, ExperimentReport& report);
  bool run_on_events(std::span<const mf::core::BookEvent> events, ExperimentReport& report);

 private:
  ExperimentConfig cfg_{};
};

std::string experiment_report_to_json(const ExperimentReport& report, const std::string& journal_path);

}  // namespace mf::research
