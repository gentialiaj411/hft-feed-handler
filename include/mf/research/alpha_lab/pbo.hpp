#pragma once

#include <cstddef>
#include <vector>

namespace mf::research::alpha_lab {

struct PboInput {
  std::vector<std::vector<double>> fold_is_scores{};
  std::vector<std::vector<double>> fold_oos_scores{};
};

struct PboResult {
  double probability{0.5};
  std::size_t n_comparisons{0};
  std::size_t n_overfit{0};
};

[[nodiscard]] PboResult compute_pbo(const PboInput& in);

}  // namespace mf::research::alpha_lab
