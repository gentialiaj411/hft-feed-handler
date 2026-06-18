#include "mf/research/alpha_lab/pbo.hpp"

#include <algorithm>

namespace mf::research::alpha_lab {

PboResult compute_pbo(const PboInput& in) {
  PboResult out{};
  const std::size_t n_folds = in.fold_is_scores.size();
  if (n_folds == 0 || in.fold_oos_scores.size() != n_folds) {
    return out;
  }

  for (std::size_t f = 0; f < n_folds; ++f) {
    if (in.fold_is_scores[f].empty() || in.fold_oos_scores[f].empty()) {
      continue;
    }
    const double is_best = *std::max_element(in.fold_is_scores[f].begin(), in.fold_is_scores[f].end());
    const double oos_best = *std::max_element(in.fold_oos_scores[f].begin(), in.fold_oos_scores[f].end());
    ++out.n_comparisons;
    if (is_best > oos_best) {
      ++out.n_overfit;
    }
  }

  if (out.n_comparisons > 0) {
    out.probability = static_cast<double>(out.n_overfit) / static_cast<double>(out.n_comparisons);
  }
  return out;
}

}  // namespace mf::research::alpha_lab
