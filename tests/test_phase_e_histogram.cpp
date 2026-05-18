#include <cassert>

#include "mf/bench/histogram.hpp"

int main() {
  mf::bench::LatencyHistogram h(1, 1'000'000'000, 3);
  for (std::uint64_t i = 1; i <= 1'000'000; ++i) {
    h.record(i % 1000 + 1);
  }
  assert(h.count() == 1'000'000);
  const auto p50 = h.percentile(0.50);
  const auto p99 = h.percentile(0.99);
  assert(p50 > 0 && p50 <= 1000);
  assert(p99 >= p50 && p99 <= 1000);
  assert(h.max() == 1000);

  mf::bench::LatencyHistogram h2(1, 1'000'000'000, 3);
  h2.record(42);
  h.merge(h2);
  assert(h.count() == 1'000'001);
  assert(h.max() == 1000);
  return 0;
}
