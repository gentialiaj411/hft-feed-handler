#pragma once

#include <string>
#include <vector>

#include "mf/research/alpha_lab/purged_cv.hpp"
#include "mf/research/alpha_lab/signal_evaluator.hpp"
#include "mf/research/alpha_lab/types.hpp"

namespace mf::research::alpha_lab {

struct StrategyBridgeResult {
  std::string signal{};
  std::size_t fold_id{0};
  double sharpe{0.0};
  std::uint64_t fills{0};
};

class StrategyBridge {
 public:
  [[nodiscard]] std::vector<StrategyBridgeResult> evaluate_top_signal(
      const std::string& signal_name,
      const std::vector<LabeledFeatureRow>& rows,
      const std::vector<CvFold>& folds,
      const std::vector<double>& signal_values) const;

  [[nodiscard]] std::vector<AblationRow> run_ablation(
      const SignalAggregate& top_signal,
      const std::vector<LabeledFeatureRow>& rows,
      const std::vector<CvFold>& folds,
      const std::vector<double>& signal_values,
      double baseline_sharpe) const;
};

}  // namespace mf::research::alpha_lab
