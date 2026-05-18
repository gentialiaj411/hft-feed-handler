#include "mf/phase4/strategy.hpp"

#include <algorithm>
#include <cmath>

namespace mf::phase4 {

MarketMakingStrategy::MarketMakingStrategy(IOrderRouter* router)
    : MarketMakingStrategy(router, Config{}) {}
MarketMakingStrategy::MarketMakingStrategy(IOrderRouter* router, Config cfg)
    : router_(router), cfg_(cfg) {}

void MarketMakingStrategy::requote(
    std::uint64_t symbol_u64,
    SymbolState& st,
    std::int64_t bid_px,
    std::int64_t ask_px,
    std::uint64_t ts_ns) {
  if (router_ == nullptr) {
    return;
  }
  if (st.bid.active) {
    router_->cancel(st.bid.order_id, ts_ns);
    st.bid = {};
  }
  if (st.ask.active) {
    router_->cancel(st.ask.order_id, ts_ns);
    st.ask = {};
  }

  st.bid = {next_order_id_++, bid_px, true};
  st.ask = {next_order_id_++, ask_px, true};
  router_->submit(symbol_u64, OrderSide::Buy, bid_px, cfg_.quote_size, ts_ns, st.bid.order_id);
  router_->submit(symbol_u64, OrderSide::Sell, ask_px, cfg_.quote_size, ts_ns, st.ask.order_id);
  st.last_requote_ts = ts_ns;
}

void MarketMakingStrategy::on_feature(const mf::phase3::FeatureVector& fv) {
  auto& st = by_symbol_[fv.symbol_u64];
  const std::int64_t inv = std::clamp(st.inventory, -cfg_.max_inventory, cfg_.max_inventory);
  const double center =
      fv.microprice - static_cast<double>(inv) * cfg_.inventory_skew_ticks_per_unit - fv.ofi * cfg_.ofi_skew_coef;
  const std::int64_t center_ticks = static_cast<std::int64_t>(std::llround(center));
  const std::int64_t bid_px = center_ticks - cfg_.half_spread_ticks;
  const std::int64_t ask_px = center_ticks + cfg_.half_spread_ticks;

  const bool refresh_by_first = !st.has_center;
  const bool refresh_by_move = st.has_center && std::llabs(center_ticks - st.last_center) >= cfg_.requote_threshold_ticks;
  const bool cooldown_ok = (fv.exchange_ts_ns >= st.last_requote_ts) &&
                           ((fv.exchange_ts_ns - st.last_requote_ts) >= cfg_.cancel_replace_cooldown_ns);

  if ((refresh_by_first || refresh_by_move) && cooldown_ok) {
    requote(fv.symbol_u64, st, bid_px, ask_px, fv.exchange_ts_ns);
    st.last_center = center_ticks;
    st.has_center = true;
  }
}

void MarketMakingStrategy::on_fill(const Fill& fill) {
  for (auto& [symbol, st] : by_symbol_) {
    if ((st.bid.active && st.bid.order_id == fill.order_id) ||
        (st.ask.active && st.ask.order_id == fill.order_id)) {
      if (fill.side == OrderSide::Buy) {
        st.inventory += static_cast<std::int64_t>(fill.qty);
      } else {
        st.inventory -= static_cast<std::int64_t>(fill.qty);
      }
      if (!fill.partial) {
        if (st.bid.active && st.bid.order_id == fill.order_id) {
          st.bid = {};
        }
        if (st.ask.active && st.ask.order_id == fill.order_id) {
          st.ask = {};
        }
      }
      st.has_center = false;
      break;
    }
  }
}

void MarketMakingStrategy::on_tick_end(std::uint64_t) {}

}  // namespace mf::phase4
