#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/phase3/types.hpp"

namespace mf::phase3 {

class OrderBookEngine {
 public:
  OrderBookEngine();

  struct ApplyResult {
    bool changed_top{false};
    double queue_ahead_before_add{0.0};
    TopOfBook top_after{};
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
  struct OrderSlot {
    std::uint64_t key{0};
    OrderRef value{};
    std::uint8_t state{0};
  };
  struct VenueBook {
    using Level = std::pair<std::uint32_t, std::uint64_t>;

    VenueBook();

    std::vector<Level> bids{};
    std::vector<Level> asks{};
  };

  static std::size_t venue_index(mf::core::Venue venue) noexcept;
  static std::uint64_t order_key(mf::core::Venue venue, std::uint64_t order_id) noexcept;
  static std::uint64_t mix_key(std::uint64_t key) noexcept;
  static TopOfBook snapshot(const VenueBook& book) noexcept;
  static std::uint64_t qty_at(const std::vector<VenueBook::Level>& levels, std::uint32_t price, bool descending) noexcept;
  static void add_qty(std::vector<VenueBook::Level>& levels, std::uint32_t price, std::uint32_t qty, bool descending);
  static void reduce_qty(std::vector<VenueBook::Level>& levels, std::uint32_t price, std::uint32_t qty, bool descending) noexcept;
  VenueBook& book_for(mf::core::Venue venue, std::uint64_t symbol_u64);
  const VenueBook* find_book(mf::core::Venue venue, std::uint64_t symbol_u64) const noexcept;
  OrderRef* find_order(std::uint64_t key) noexcept;
  void put_order(std::uint64_t key, const OrderRef& value) noexcept;
  void erase_order(std::uint64_t key) noexcept;

  std::array<std::unordered_map<std::uint64_t, VenueBook>, 3> books_{};
  std::vector<OrderSlot> orders_{};
  std::size_t order_mask_{0};
  std::size_t order_count_{0};
  std::array<std::uint64_t, 3> cached_symbols_{};
  std::array<VenueBook*, 3> cached_books_{};
  std::array<bool, 3> cached_valid_{};
};

}  // namespace mf::phase3
