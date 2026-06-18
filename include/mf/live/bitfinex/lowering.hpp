#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/live/bitfinex/fixed_point.hpp"
#include "mf/live/bitfinex/on_book.hpp"
#include "mf/live/bitfinex/wire_types.hpp"

namespace mf::live::bitfinex {

struct LoweringStats {
  FixedPointStats fixed_point{};
  std::uint64_t adds{0};
  std::uint64_t replaces{0};
  std::uint64_t cancels{0};
  std::uint64_t skipped_not_on_book{0};
  std::uint64_t rejected{0};
};

void symbol_from_wire(const std::string& wire, mf::core::SymbolKey& out) noexcept;

[[nodiscard]] std::optional<mf::core::BookEvent> lower_row(
    const BookRow& row,
    std::uint64_t sequence,
    std::uint64_t ts_ns,
    const std::string& symbol_wire,
    OnBookTracker& on_book,
    LoweringStats& stats) noexcept;

[[nodiscard]] std::vector<mf::core::BookEvent> lower_snapshot_rows(
    const std::vector<BookRow>& rows,
    std::uint64_t ts_ns,
    const std::string& symbol_wire,
    OnBookTracker& on_book,
    LoweringStats& stats) noexcept;

}  // namespace mf::live::bitfinex
