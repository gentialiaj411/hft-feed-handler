#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>

#include "mf/phase3/types.hpp"
#include "mf/phase4/sim_matching_engine.hpp"

namespace mf::phase4 {

class IOrderRouter {
 public:
  virtual ~IOrderRouter() = default;
  virtual void submit(std::uint64_t symbol_u64, OrderSide side, std::int64_t price, std::uint64_t qty, std::uint64_t ts_ns, std::uint64_t order_id) = 0;
  virtual void cancel(std::uint64_t order_id, std::uint64_t ts_ns) = 0;
};

class IStrategy {
 public:
  virtual ~IStrategy() = default;
  virtual void on_feature(const mf::phase3::FeatureVector& fv) = 0;
  virtual void on_fill(const Fill& fill) = 0;
  virtual void on_tick_end(std::uint64_t ts_ns) = 0;
};

class MarketMakingStrategy final : public IStrategy {
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

  explicit MarketMakingStrategy(IOrderRouter* router);
  MarketMakingStrategy(IOrderRouter* router, Config cfg);

  void on_feature(const mf::phase3::FeatureVector& fv) override;
  void on_fill(const Fill& fill) override;
  void on_tick_end(std::uint64_t ts_ns) override;

 private:
  struct SideOrder {
    std::uint64_t order_id{0};
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

  void requote(std::uint64_t symbol_u64, SymbolState& st, std::int64_t bid_px, std::int64_t ask_px, std::uint64_t ts_ns);

  IOrderRouter* router_{nullptr};
  Config cfg_{};
  std::unordered_map<std::uint64_t, SymbolState> by_symbol_{};
  std::uint64_t next_order_id_{1};
};

}  // namespace mf::phase4
