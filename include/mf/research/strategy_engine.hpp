#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "mf/phase3/types.hpp"
#include "mf/phase4/sim_matching_engine.hpp"

namespace mf::research {

enum class OrderAction : std::uint8_t { Submit = 0, Cancel = 1 };

struct OrderIntent {
  OrderAction action{OrderAction::Submit};
  std::uint64_t symbol_u64{0};
  mf::phase4::OrderSide side{mf::phase4::OrderSide::Buy};
  std::int64_t price{0};
  std::uint64_t qty{0};
  std::uint64_t ts_ns{0};
  std::uint64_t order_id{0};
};

class IOrderIntentSink {
 public:
  virtual ~IOrderIntentSink() = default;
  virtual void on_intent(const OrderIntent& intent) = 0;
};

class StrategyEngine {
 public:
  struct Config {
    std::int64_t half_spread_ticks{1};
    std::uint64_t quote_size{10};
    std::int64_t max_inventory{1'000};
    double inventory_skew_ticks_per_unit{0.0};
    double ofi_skew_coef{0.0};
    std::int64_t requote_threshold_ticks{1};
    std::uint64_t cancel_replace_cooldown_ns{0};
  };

  explicit StrategyEngine(IOrderIntentSink* sink);
  StrategyEngine(IOrderIntentSink* sink, Config cfg);

  void on_feature(const mf::phase3::FeatureVector& fv);
  void on_fill(std::uint64_t symbol_u64, const mf::phase4::Fill& fill);

 private:
  struct SideOrder {
    std::uint64_t order_id{0};
    mf::phase4::OrderSide side{mf::phase4::OrderSide::Buy};
    std::int64_t price{0};
    bool active{false};
  };
  struct SymbolState {
    std::int64_t inventory{0};
    SideOrder bid{};
    SideOrder ask{};
    std::int64_t last_center{0};
    bool has_center{false};
    std::uint64_t last_requote_ts{0};
  };

  void emit_cancel(SideOrder& order, std::uint64_t symbol_u64, std::uint64_t ts_ns);
  void emit_submit(std::uint64_t symbol_u64,
                   SymbolState& st,
                   mf::phase4::OrderSide side,
                   std::int64_t price,
                   std::uint64_t ts_ns);

  IOrderIntentSink* sink_{nullptr};
  Config cfg_{};
  std::vector<std::pair<std::uint64_t, SymbolState>> states_{};
  std::uint64_t next_order_id_{1};
};

}  // namespace mf::research
