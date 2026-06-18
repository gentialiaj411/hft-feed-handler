#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "mf/research/alpha_lab/capacity_sweep.hpp"
#include "mf/research/alpha_lab/deflated_sharpe.hpp"
#include "mf/research/alpha_lab/pbo.hpp"
#include "mf/research/alpha_lab/purged_cv.hpp"
#include "mf/research/alpha_lab/signal_evaluator.hpp"
#include "mf/research/alpha_lab/types.hpp"

namespace mf::research::alpha_lab {

struct TearsheetInput {
  std::string journal{};
  MaterializeStats materialize{};
  std::vector<CvFold> folds{};
  std::vector<SignalAggregate> signals{};
  std::vector<SignalAggregate> failed_signals{};
  std::size_t n_trials{0};
  std::vector<DeflatedSharpeResult> dsr_results{};
  std::vector<PboResult> pbo_results{};
  std::vector<CapacityPoint> capacity_curve{};
  double capacity_ceiling{0.0};
  std::vector<AblationRow> ablation{};
  double baseline_sharpe{0.10279};
};

class TearsheetWriter {
 public:
  bool write_markdown(const std::string& path, const TearsheetInput& in) const;
  bool write_json(const std::string& path, const TearsheetInput& in) const;
  bool write_signal_survival_csv(const std::string& path, const TearsheetInput& in) const;
  bool write_capacity_csv(const std::string& path, const std::vector<CapacityPoint>& curve) const;
  bool write_folds_json(const std::string& path, const std::vector<CvFold>& folds) const;
  bool write_ablation_md(const std::string& path, const std::vector<AblationRow>& rows) const;
};

}  // namespace mf::research::alpha_lab
