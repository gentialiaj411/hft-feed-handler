#include "mf/bench/histogram.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace mf::bench {

LatencyHistogram::LatencyHistogram(std::uint64_t min_ns, std::uint64_t max_ns, int significant_digits)
    : min_ns_(std::max<std::uint64_t>(1, min_ns)),
      max_ns_(std::max(min_ns_, max_ns)),
      significant_digits_(significant_digits) {
  bins_per_octave_ = (significant_digits_ <= 1) ? 16U : (significant_digits_ == 2 ? 32U : 64U);
  const double octaves = std::log2(static_cast<double>(max_ns_) / static_cast<double>(min_ns_)) + 1.0;
  const std::size_t n = static_cast<std::size_t>(octaves * static_cast<double>(bins_per_octave_)) + 1U;
  buckets_.assign(n, 0);
}

std::size_t LatencyHistogram::index_for(std::uint64_t ns) const noexcept {
  const std::uint64_t clamped = std::max(min_ns_, std::min(max_ns_, ns));
  const double ratio = static_cast<double>(clamped) / static_cast<double>(min_ns_);
  const double raw = std::log2(ratio) * static_cast<double>(bins_per_octave_);
  const std::size_t idx = static_cast<std::size_t>(raw);
  return std::min<std::size_t>(idx, buckets_.size() - 1U);
}

std::uint64_t LatencyHistogram::value_for_index(std::size_t idx) const noexcept {
  const double x = static_cast<double>(idx) / static_cast<double>(bins_per_octave_);
  const double v = static_cast<double>(min_ns_) * std::pow(2.0, x);
  return static_cast<std::uint64_t>(v);
}

void LatencyHistogram::record(std::uint64_t ns) noexcept {
  const std::size_t idx = index_for(ns);
  ++buckets_[idx];
  ++total_count_;
  if (ns > max_observed_) max_observed_ = ns;
}

std::uint64_t LatencyHistogram::percentile(double p) const noexcept {
  if (total_count_ == 0) return 0;
  const double bounded = std::max(0.0, std::min(1.0, p));
  const std::uint64_t target = static_cast<std::uint64_t>(bounded * static_cast<double>(total_count_ - 1));
  std::uint64_t acc = 0;
  for (std::size_t i = 0; i < buckets_.size(); ++i) {
    acc += buckets_[i];
    if (acc > target) return value_for_index(i);
  }
  return max_observed_;
}

void LatencyHistogram::merge(const LatencyHistogram& other) noexcept {
  if (other.buckets_.size() != buckets_.size()) return;
  for (std::size_t i = 0; i < buckets_.size(); ++i) buckets_[i] += other.buckets_[i];
  total_count_ += other.total_count_;
  max_observed_ = std::max(max_observed_, other.max_observed_);
}

std::string LatencyHistogram::to_json_buckets() const {
  std::ostringstream os;
  os << "[";
  bool first = true;
  for (std::size_t i = 0; i < buckets_.size(); ++i) {
    const auto c = buckets_[i];
    if (c == 0) continue;
    if (!first) os << ",";
    first = false;
    os << "{\"ns\":" << value_for_index(i) << ",\"count\":" << c << "}";
  }
  os << "]";
  return os.str();
}

}  // namespace mf::bench
