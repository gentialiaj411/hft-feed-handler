#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>

#include "mf/core/types.hpp"
#include "mf/phase3/types.hpp"

namespace mf::phase3 {

class NbboConsolidator {
 public:
  [[nodiscard]] bool update(std::uint64_t symbol_u64, mf::core::Venue venue, const TopOfBook& top) noexcept;
  [[nodiscard]] Nbbo current(std::uint64_t symbol_u64) const noexcept;

 private:
  struct VenueTop {
    bool has_bid{false};
    bool has_ask{false};
    std::uint32_t bid_price{0};
    std::uint32_t bid_qty{0};
    std::uint32_t ask_price{0};
    std::uint32_t ask_qty{0};
  };
  struct SymbolState {
    std::array<VenueTop, 3> venues{};
    Nbbo nbbo{};
  };

  static std::size_t venue_index(mf::core::Venue venue) noexcept;
  static Nbbo compute(const SymbolState& state) noexcept;

  std::unordered_map<std::uint64_t, SymbolState> by_symbol_{};
};

}  // namespace mf::phase3
