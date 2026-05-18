#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "mf/core/types.hpp"

namespace mf::phase4 {

enum class OrderSide : std::uint8_t { Buy = 0, Sell = 1 };

struct Fill {
  std::uint64_t order_id{0};
  OrderSide side{OrderSide::Buy};
  std::int64_t price{0};
  std::uint64_t qty{0};
  std::uint64_t ts_ns{0};
  bool partial{false};
};

/*
Fill model (conservative passive simulation):
1) Strategy orders rest only in a private sim book; no impact on public tape/book.
2) Only tape prints (`Trade`, `ExecutePrice`, `CrossTrade`) may trigger fills.
3) Tape sell-side prints can fill resting bids priced at or above print price.
4) Tape buy-side prints can fill resting asks priced at or below print price.
5) Matching is strict price-time priority within strategy-owned orders only.
6) Filled quantity per print is capped by participation fraction * tape print qty.
7) Default participation cap is 25%, configurable in constructor.
8) No aggressive strategy crossing and no queue-position alpha is assumed.
9) If tape side is unknown, no fills are generated.
10) Timestamps on fills use exchange timestamp from triggering tape event.
*/
class SimMatchingEngine {
 public:
  explicit SimMatchingEngine(double participation_cap = 0.25);

  void bind_order_symbol(std::uint64_t order_id, std::uint64_t symbol_u64);
  void submit(OrderSide side, std::int64_t price, std::uint64_t qty, std::uint64_t order_id, std::uint64_t ts_ns);
  void cancel(std::uint64_t order_id, std::uint64_t ts_ns);
  void on_market_event(const mf::core::BookEvent& ev);

  void set_fill_callback(std::function<void(const Fill&)> cb) { on_fill_ = std::move(cb); }

 private:
  struct Order {
    std::uint64_t order_id{0};
    OrderSide side{OrderSide::Buy};
    std::int64_t price{0};
    std::uint64_t qty{0};
    std::uint64_t ts_ns{0};
  };

  struct SymbolBook {
    std::vector<Order> bids{};
    std::vector<Order> asks{};
  };

  static bool bid_better(const Order& a, const Order& b) noexcept;
  static bool ask_better(const Order& a, const Order& b) noexcept;
  void fill_bids(SymbolBook& book, std::int64_t trade_price, std::uint64_t fill_budget, std::uint64_t ts_ns);
  void fill_asks(SymbolBook& book, std::int64_t trade_price, std::uint64_t fill_budget, std::uint64_t ts_ns);

  double participation_cap_{0.25};
  std::unordered_map<std::uint64_t, SymbolBook> books_{};
  std::unordered_map<std::uint64_t, std::uint64_t> order_to_symbol_{};
  std::function<void(const Fill&)> on_fill_{};
};

}  // namespace mf::phase4
