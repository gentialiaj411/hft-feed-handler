#pragma once

#include <cstdint>
#include <vector>

#include "mf/research/alpha_lab/types.hpp"

namespace mf::research::alpha_lab {

struct CvFold {
  std::size_t fold_id{0};
  std::uint64_t test_start_ns{0};
  std::uint64_t test_end_ns{0};
  std::vector<std::size_t> train_indices{};
  std::vector<std::size_t> test_indices{};
};

struct PurgedCvConfig {
  std::size_t n_folds{5};
  std::uint64_t purge_ns{2'000'000'000ULL};
  std::uint64_t embargo_ns{1'000'000'000ULL};
};

class PurgedCv {
 public:
  PurgedCv();
  explicit PurgedCv(PurgedCvConfig cfg);

  [[nodiscard]] std::vector<CvFold> split(
      const std::vector<LabeledFeatureRow>& rows) const;

  [[nodiscard]] bool is_leaky_split(
      const CvFold& fold,
      std::uint64_t label_horizon_ns,
      const std::vector<LabeledFeatureRow>& rows) const;

 private:
  PurgedCvConfig cfg_{};
};

}  // namespace mf::research::alpha_lab
