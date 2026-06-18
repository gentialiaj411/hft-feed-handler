#pragma once

#include <cstdint>

namespace mf::live::bitfinex {

struct FixedPointStats {
  std::uint64_t price_reject{0};
  std::uint64_t qty_reject{0};
};

[[nodiscard]] bool price_to_u32_4dp(double price, std::uint32_t& out, FixedPointStats& stats) noexcept;
[[nodiscard]] bool qty_to_u32_micro_base(double abs_amount, std::uint32_t& out, FixedPointStats& stats) noexcept;

}  // namespace mf::live::bitfinex
