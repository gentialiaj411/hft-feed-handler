#include "mf/live/bitfinex/fixed_point.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mf::live::bitfinex {

namespace {

bool to_u32_bounded(double v, std::uint32_t& out, std::uint64_t& reject_counter) noexcept {
  if (!std::isfinite(v) || v < 0.0) {
    ++reject_counter;
    return false;
  }
  const double rounded = std::round(v);
  if (rounded > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
    ++reject_counter;
    return false;
  }
  out = static_cast<std::uint32_t>(rounded);
  return true;
}

}  // namespace

bool price_to_u32_4dp(double price, std::uint32_t& out, FixedPointStats& stats) noexcept {
  return to_u32_bounded(price * 10000.0, out, stats.price_reject);
}

bool qty_to_u32_micro_base(double abs_amount, std::uint32_t& out, FixedPointStats& stats) noexcept {
  return to_u32_bounded(abs_amount * 1000000.0, out, stats.qty_reject);
}

}  // namespace mf::live::bitfinex
