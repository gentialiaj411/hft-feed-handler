#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mf::bench {

// Bucket scheme:
// - Buckets are logarithmic over [min_ns, max_ns] with fixed bins-per-octave.
// - bucket_index ~= log2(value/min_ns) * bins_per_octave.
// - record() is allocation-free in steady state (pre-sized bucket vector).
class LatencyHistogram {
 public:
  explicit LatencyHistogram(std::uint64_t min_ns = 1, std::uint64_t max_ns = 1'000'000'000, int significant_digits = 3);

  void record(std::uint64_t ns) noexcept;
  [[nodiscard]] std::uint64_t percentile(double p) const noexcept;
  [[nodiscard]] std::uint64_t max() const noexcept { return max_observed_; }
  [[nodiscard]] std::uint64_t count() const noexcept { return total_count_; }
  void merge(const LatencyHistogram& other) noexcept;

  [[nodiscard]] std::string to_json_buckets() const;

 private:
  std::size_t index_for(std::uint64_t ns) const noexcept;
  std::uint64_t value_for_index(std::size_t idx) const noexcept;

  std::uint64_t min_ns_{1};
  std::uint64_t max_ns_{1'000'000'000};
  int significant_digits_{3};
  std::uint32_t bins_per_octave_{64};
  std::vector<std::uint64_t> buckets_{};
  std::uint64_t total_count_{0};
  std::uint64_t max_observed_{0};
};

}  // namespace mf::bench
