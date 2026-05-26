#pragma once

#include <cstdint>
#include <unordered_map>

#include "mf/core/types.hpp"
#include "mf/phase3/order_book_engine.hpp"
#include "mf/phase4/sim_matching_engine.hpp"
#include "mf/research/signals/ofi.hpp"
#include "mf/research/strategy_engine.hpp"

namespace mf::research {

class OfiStrategy {
 public:
  struct Config {
    OfiSignal::Config signal{};
    std::uint64_t quote_size{10};
    // Half-spread (in ticks) around the quote center. Original semantics: quote at
    // mid ± half_spread_ticks, where center shifts toward bid/ask when OFI fires.
    std::int64_t half_spread_ticks{1};
    std::int64_t max_position{200};
    double threshold{100.0};
    std::int64_t aggressive_flatten_offset_ticks{0};
    // Re-quote cooldown: skip cancel/resubmit if fewer than this many events have
    // passed since the last quote with identical price. Reduces churn so resting
    // orders stay long enough to actually fill against tape prints. 0 disables.
    std::uint64_t requote_cooldown_events{0};
  };

  OfiStrategy(IOrderIntentSink* sink, Config cfg);

  void on_event(const mf::core::BookEvent& ev);
  void on_fill(std::uint64_t symbol_u64, const mf::phase4::Fill& fill);

 private:
  struct State {
    OfiSignal signal{};
    std::int64_t inventory{0};
    std::uint64_t bid_order_id{0};
    std::uint64_t ask_order_id{0};
    std::int64_t last_bid_quote{0};
    std::int64_t last_ask_quote{0};
    std::uint64_t events_since_quote{0};
    bool has_last_quote{false};
  };

  void cancel_if_active(std::uint64_t symbol_u64, std::uint64_t& order_id, mf::phase4::OrderSide side, std::int64_t price, std::uint64_t ts_ns);
  std::uint64_t submit(std::uint64_t symbol_u64, mf::phase4::OrderSide side, std::int64_t price, std::uint64_t qty, std::uint64_t ts_ns);

  IOrderIntentSink* sink_{nullptr};
  Config cfg_{};
  std::unordered_map<std::uint64_t, State> by_symbol_{};
  mf::phase3::OrderBookEngine book_engine_{};
  std::uint64_t next_order_id_{1};
};

}  // namespace mf::research
