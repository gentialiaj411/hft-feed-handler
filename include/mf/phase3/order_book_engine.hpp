#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>

#include "mf/core/types.hpp"
#include "mf/phase3/types.hpp"

namespace mf::phase3 {

class OrderBookEngine {
 public:
  struct ApplyResult {
    bool changed_top{false};
    double queue_ahead_before_add{0.0};
  };

  [[nodiscard]] ApplyResult on_event(const mf::core::BookEvent& ev) noexcept;
  [[nodiscard]] TopOfBook top_of_book(mf::core::Venue venue, std::uint64_t symbol_u64) const noexcept;

 private:
  struct OrderRef {
    mf::core::Venue venue{mf::core::Venue::Nasdaq};
    std::uint64_t symbol_u64{0};
    mf::core::Side side{mf::core::Side::Unknown};
    std::uint32_t price{0};
    std::uint32_t qty{0};
    double queue_ahead{0.0};
  };
  struct VenueBook {
    std::map<std::uint32_t, std::uint64_t, std::greater<>> bids{};
    std::map<std::uint32_t, std::uint64_t> asks{};
  };

  static std::size_t venue_index(mf::core::Venue venue) noexcept;
  static std::uint64_t order_key(mf::core::Venue venue, std::uint64_t order_id) noexcept;
  static TopOfBook snapshot(const VenueBook& book) noexcept;
  VenueBook& book_for(mf::core::Venue venue, std::uint64_t symbol_u64);
  const VenueBook* find_book(mf::core::Venue venue, std::uint64_t symbol_u64) const noexcept;

  std::array<std::unordered_map<std::uint64_t, VenueBook>, 3> books_{};
  std::unordered_map<std::uint64_t, OrderRef> orders_{};
};

}  // namespace mf::phase3
