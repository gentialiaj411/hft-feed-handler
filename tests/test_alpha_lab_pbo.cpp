#include <cassert>
#include <vector>

#include "mf/research/alpha_lab/pbo.hpp"

int main() {
  mf::research::alpha_lab::PboInput structured{};
  structured.fold_is_scores = {{1.0, 0.8, 0.6}, {0.9, 0.7, 0.5}};
  structured.fold_oos_scores = {{0.2, 0.1, 0.0}, {0.3, 0.2, 0.1}};
  const auto structured_result = mf::research::alpha_lab::compute_pbo(structured);
  assert(structured_result.n_comparisons == 2);
  assert(structured_result.probability > 0.5);

  mf::research::alpha_lab::PboInput random{};
  random.fold_is_scores = {{0.1, -0.2, 0.05}, {-0.1, 0.0, 0.2}};
  random.fold_oos_scores = {{0.0, 0.1, -0.1}, {0.05, -0.05, 0.1}};
  const auto random_result = mf::research::alpha_lab::compute_pbo(random);
  assert(random_result.n_comparisons == 2);
  assert(random_result.probability >= 0.0 && random_result.probability <= 1.0);
  return 0;
}
