#pragma once

#include <cstdint>
#include <string>

#include "mf/phase4/pnl_accountant.hpp"
#include "mf/phase4/sim_matching_engine.hpp"
#include "mf/research/event_store.hpp"
#include "mf/research/strategy_engine.hpp"

namespace mf::research {

struct ExperimentConfig {
  StrategyEngine::Config strategy{};
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
};

class ExperimentRunner {
 public:
  ExperimentRunner();
  explicit ExperimentRunner(ExperimentConfig cfg);

  bool run(const EventStore& store, ExperimentReport& report);

 private:
  ExperimentConfig cfg_{};
};

std::string experiment_report_to_json(const ExperimentReport& report, const std::string& journal_path);

}  // namespace mf::research
