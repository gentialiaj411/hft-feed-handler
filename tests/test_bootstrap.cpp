#include <cassert>
#include <cstdio>
#include <vector>

#include "mf/research/metrics/bootstrap.hpp"

int main() {
  const std::vector<double> positive = {1.0, 2.0, 1.5, 2.5, 1.0, 3.0};
  const auto r = mf::research::block_bootstrap_mean_pvalue(positive, 2, 500, 7);
  assert(r.observed_mean > 0.0);
  assert(r.p_value_greater_than_zero < 0.05);

  const std::vector<double> flat = {0.0, 0.0, 0.0, 0.0};
  const auto r2 = mf::research::block_bootstrap_mean_pvalue(flat, 2, 200, 9);
  assert(r2.p_value_greater_than_zero >= 0.5);

  std::printf("PASS bootstrap p=%.4f mean=%.4f\n", r.p_value_greater_than_zero, r.observed_mean);
  return 0;
}
