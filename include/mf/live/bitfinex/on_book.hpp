#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace mf::live::bitfinex {

struct RestingOrder {
  std::uint32_t price{0};
  std::uint32_t qty{0};
};

class OnBookTracker {
 public:
  [[nodiscard]] bool contains(std::uint64_t order_id) const noexcept;
  [[nodiscard]] std::optional<RestingOrder> get(std::uint64_t order_id) const noexcept;
  void insert(std::uint64_t order_id, std::uint32_t price, std::uint32_t qty) noexcept;
  void update(std::uint64_t order_id, std::uint32_t price, std::uint32_t qty) noexcept;
  void erase(std::uint64_t order_id) noexcept;
  void clear() noexcept { orders_.clear(); }
  [[nodiscard]] std::size_t size() const noexcept { return orders_.size(); }

 private:
  std::unordered_map<std::uint64_t, RestingOrder> orders_{};
};

}  // namespace mf::live::bitfinex
