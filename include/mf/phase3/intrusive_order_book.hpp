#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/phase3/book_snapshot.hpp"
#include "mf/phase3/types.hpp"

namespace mf::phase3 {

class IntrusiveOrderBook {
 public:
  explicit IntrusiveOrderBook(std::size_t order_capacity = 1U << 21U);

  struct ApplyResult {
    bool changed_top{false};
    bool order_pool_exhausted{false};
    double queue_ahead_before_add{0.0};
    TopOfBook top_after{};
  };

  [[nodiscard]] ApplyResult on_event(const mf::core::BookEvent& ev) noexcept;
  [[nodiscard]] TopOfBook top_of_book(mf::core::Venue venue, std::uint64_t symbol_u64) const noexcept;
  [[nodiscard]] BookSnapshot snapshot(mf::core::Venue venue, std::uint64_t symbol_u64) const;
  [[nodiscard]] std::size_t live_order_count() const noexcept { return live_order_count_; }
  [[nodiscard]] std::uint64_t dropped_order_inserts() const noexcept { return dropped_order_inserts_; }

 private:
  static constexpr std::uint32_t kNull = UINT32_MAX;

  struct OrderNode {
    std::uint64_t key{0};
    std::uint64_t symbol_u64{0};
    std::uint32_t price{0};
    std::uint32_t qty{0};
    std::uint32_t prev{kNull};
    std::uint32_t next{kNull};
    mf::core::Venue venue{mf::core::Venue::Nasdaq};
    mf::core::Side side{mf::core::Side::Unknown};
    bool live{false};
  };

  struct Level {
    std::uint32_t price{0};
    std::uint64_t qty{0};
    std::uint32_t head{kNull};
    std::uint32_t tail{kNull};
  };

  struct SideBook {
    std::unordered_map<std::uint32_t, Level> levels{};
    std::uint32_t best_price{0};
    bool has_best{false};
    bool descending{false};
  };

  struct VenueBook {
    VenueBook();
    SideBook bids{};
    SideBook asks{};
  };

  static std::size_t venue_index(mf::core::Venue venue) noexcept;
  static std::uint64_t order_key(mf::core::Venue venue, std::uint64_t order_id) noexcept;
  static TopOfBook snapshot(const VenueBook& book) noexcept;

  VenueBook& book_for(mf::core::Venue venue, std::uint64_t symbol_u64);
  const VenueBook* find_book(mf::core::Venue venue, std::uint64_t symbol_u64) const noexcept;
  SideBook& side_book(VenueBook& book, mf::core::Side side) noexcept;
  const SideBook& side_book(const VenueBook& book, mf::core::Side side) const noexcept;
  Level& find_or_add_level(SideBook& side, std::uint32_t price);
  void remove_level_if_empty(SideBook& side, std::uint32_t price) noexcept;
  void update_best_on_add(SideBook& side, std::uint32_t price) noexcept;
  void recompute_best(SideBook& side) noexcept;
  std::uint32_t allocate_node() noexcept;
  void release_node(std::uint32_t idx) noexcept;
  bool add_order(mf::core::Venue venue, std::uint64_t symbol, const mf::core::BookEvent& ev, double& queue_ahead) noexcept;
  void reduce_order(std::uint64_t key, std::uint32_t qty, bool delete_all) noexcept;
  void replace_order(mf::core::Venue venue, std::uint64_t symbol, const mf::core::BookEvent& ev) noexcept;

  std::array<std::unordered_map<std::uint64_t, VenueBook>, mf::core::kVenueSlotCount> books_{};
  std::vector<OrderNode> nodes_{};
  std::vector<std::uint32_t> free_list_{};
  std::unordered_map<std::uint64_t, std::uint32_t> order_index_{};
  std::size_t live_order_count_{0};
  std::uint64_t dropped_order_inserts_{0};
  std::array<std::uint64_t, mf::core::kVenueSlotCount> cached_symbols_{};
  std::array<VenueBook*, mf::core::kVenueSlotCount> cached_books_{};
  std::array<bool, mf::core::kVenueSlotCount> cached_valid_{};
};

}  // namespace mf::phase3
