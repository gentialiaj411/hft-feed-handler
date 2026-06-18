#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mf/research/alpha_lab/purged_cv.hpp"
#include "mf/research/alpha_lab/signal_registry.hpp"
#include "mf/research/alpha_lab/types.hpp"

namespace mf::research::alpha_lab {

struct SignalFoldMetrics {
  std::string signal{};
  std::uint64_t horizon_ns{0};
  std::size_t fold_id{0};
  double ic{0.0};
  double hit_rate{0.0};
  double quintile_spread{0.0};
  std::size_t n_obs{0};
};

struct SignalAggregate {
  std::string signal{};
  std::uint64_t horizon_ns{0};
  double mean_ic{0.0};
  double std_ic{0.0};
  double mean_quintile_spread{0.0};
  double std_quintile_spread{0.0};
  std::size_t n_folds{0};
};

class SignalEvaluator {
 public:
  [[nodiscard]] static double pearson_ic(
      const std::vector<double>& signal,
      const std::vector<double>& labels);

  [[nodiscard]] static double hit_rate(
      const std::vector<double>& signal,
      const std::vector<double>& labels);

  [[nodiscard]] static double quintile_spread(
      const std::vector<double>& signal,
      const std::vector<double>& labels);

  [[nodiscard]] static SignalFoldMetrics evaluate_fold(
      const std::string& signal_name,
      std::uint64_t horizon_ns,
      const std::vector<double>& signal,
      const std::vector<double>& labels,
      std::size_t fold_id);

  [[nodiscard]] static std::vector<SignalAggregate> aggregate(
      const std::vector<SignalFoldMetrics>& per_fold);
};

}  // namespace mf::research::alpha_lab
