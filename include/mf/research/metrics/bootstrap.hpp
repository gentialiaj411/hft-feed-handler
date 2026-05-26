#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace mf::research {

struct BlockBootstrapResult {
  double observed_mean{0.0};
  double p_value_greater_than_zero{1.0};
  std::uint64_t iterations{0};
  std::uint64_t block_size{0};
};

// Circular block bootstrap on a PnL (or return) series. p_value is the fraction of bootstrap
// sample means <= 0 (one-sided test for positive mean).
BlockBootstrapResult block_bootstrap_mean_pvalue(
    std::span<const double> series,
    std::uint64_t block_size,
    std::uint64_t iterations,
    std::uint64_t seed);

}  // namespace mf::research
