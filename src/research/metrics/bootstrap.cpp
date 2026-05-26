#include "mf/research/metrics/bootstrap.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace mf::research {

BlockBootstrapResult block_bootstrap_mean_pvalue(
    const std::span<const double> series,
    std::uint64_t block_size,
    const std::uint64_t iterations,
    const std::uint64_t seed) {
  BlockBootstrapResult out{};
  out.iterations = iterations;
  if (series.empty() || iterations == 0) {
    return out;
  }

  out.block_size = std::max<std::uint64_t>(1, block_size);
  if (out.block_size > series.size()) {
    out.block_size = series.size();
  }

  out.observed_mean = std::accumulate(series.begin(), series.end(), 0.0) / static_cast<double>(series.size());

  std::mt19937_64 rng(seed);
  std::uniform_int_distribution<std::size_t> start_dist(0, series.size() - 1);

  std::vector<double> sample;
  sample.reserve(series.size());
  std::uint64_t le_zero = 0;

  for (std::uint64_t it = 0; it < iterations; ++it) {
    sample.clear();
    while (sample.size() < series.size()) {
      const std::size_t start = start_dist(rng);
      for (std::uint64_t i = 0; i < out.block_size && sample.size() < series.size(); ++i) {
        sample.push_back(series[(start + i) % series.size()]);
      }
    }
    const double mean = std::accumulate(sample.begin(), sample.end(), 0.0) / static_cast<double>(sample.size());
    if (mean <= 0.0) {
      ++le_zero;
    }
  }

  out.p_value_greater_than_zero = 1.0 - (static_cast<double>(le_zero) / static_cast<double>(iterations));
  return out;
}

}  // namespace mf::research
