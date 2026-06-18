#include "mf/live/bitfinex/on_book.hpp"

namespace mf::live::bitfinex {

bool OnBookTracker::contains(std::uint64_t order_id) const noexcept {
  return orders_.find(order_id) != orders_.end();
}

std::optional<RestingOrder> OnBookTracker::get(std::uint64_t order_id) const noexcept {
  const auto it = orders_.find(order_id);
  if (it == orders_.end()) return std::nullopt;
  return it->second;
}

void OnBookTracker::insert(std::uint64_t order_id, std::uint32_t price, std::uint32_t qty) noexcept {
  orders_[order_id] = RestingOrder{price, qty};
}

void OnBookTracker::update(std::uint64_t order_id, std::uint32_t price, std::uint32_t qty) noexcept {
  orders_[order_id] = RestingOrder{price, qty};
}

void OnBookTracker::erase(std::uint64_t order_id) noexcept {
  orders_.erase(order_id);
}

}  // namespace mf::live::bitfinex
