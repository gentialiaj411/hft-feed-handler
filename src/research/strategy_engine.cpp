#include "mf/research/strategy_engine.hpp"

#include <algorithm>
#include <cstdlib>
#include <cmath>

namespace mf::research {

StrategyEngine::StrategyEngine(IOrderIntentSink* sink) : StrategyEngine(sink, Config{}) {}

StrategyEngine::StrategyEngine(IOrderIntentSink* sink, Config cfg) : sink_(sink), cfg_(cfg) {}

void StrategyEngine::emit_cancel(SideOrder& order, std::uint64_t symbol_u64, std::uint64_t ts_ns) {
  if (sink_ == nullptr || !order.active) {
    return;
  }
  sink_->on_intent(OrderIntent{
      OrderAction::Cancel,
      symbol_u64,
      order.side,
      order.price,
      0,
      ts_ns,
      order.order_id});
  order = {};
}

void StrategyEngine::emit_submit(
    std::uint64_t symbol_u64,
    SymbolState& st,
    mf::phase4::OrderSide side,
    std::int64_t price,
    std::uint64_t ts_ns) {
  if (sink_ == nullptr) {
    return;
  }
  const std::uint64_t order_id = next_order_id_++;
  if (side == mf::phase4::OrderSide::Buy) {
    st.bid = {order_id, side, price, true};
  } else {
    st.ask = {order_id, side, price, true};
  }
  sink_->on_intent(OrderIntent{
      OrderAction::Submit,
      symbol_u64,
      side,
      price,
      cfg_.quote_size,
      ts_ns,
      order_id});
}

void StrategyEngine::on_feature(const mf::phase3::FeatureVector& fv) {
  auto it = std::find_if(states_.begin(), states_.end(), [&](const auto& entry) {
    return entry.first == fv.symbol_u64;
  });
  if (it == states_.end()) {
    states_.push_back({fv.symbol_u64, SymbolState{}});
    it = states_.end() - 1;
  }

  auto& st = it->second;
  const std::int64_t inv = std::clamp(st.inventory, -cfg_.max_inventory, cfg_.max_inventory);
  const double center =
      fv.microprice - static_cast<double>(inv) * cfg_.inventory_skew_ticks_per_unit - fv.ofi * cfg_.ofi_skew_coef;
  const std::int64_t center_ticks = static_cast<std::int64_t>(std::llround(center));
  const bool refresh_by_first = !st.has_center;
  const bool refresh_by_move = st.has_center && std::llabs(center_ticks - st.last_center) >= cfg_.requote_threshold_ticks;
  const bool cooldown_ok = (fv.exchange_ts_ns >= st.last_requote_ts) &&
                           ((fv.exchange_ts_ns - st.last_requote_ts) >= cfg_.cancel_replace_cooldown_ns);
  if ((!refresh_by_first && !refresh_by_move) || !cooldown_ok) {
    return;
  }

  emit_cancel(st.bid, fv.symbol_u64, fv.exchange_ts_ns);
  emit_cancel(st.ask, fv.symbol_u64, fv.exchange_ts_ns);
  emit_submit(fv.symbol_u64, st, mf::phase4::OrderSide::Buy, center_ticks - cfg_.half_spread_ticks, fv.exchange_ts_ns);
  emit_submit(fv.symbol_u64, st, mf::phase4::OrderSide::Sell, center_ticks + cfg_.half_spread_ticks, fv.exchange_ts_ns);
  st.last_center = center_ticks;
  st.has_center = true;
  st.last_requote_ts = fv.exchange_ts_ns;
}

void StrategyEngine::on_fill(std::uint64_t symbol_u64, const mf::phase4::Fill& fill) {
  auto it = std::find_if(states_.begin(), states_.end(), [&](const auto& entry) {
    return entry.first == symbol_u64;
  });
  if (it == states_.end()) {
    return;
  }
  auto& st = it->second;
  const bool is_bid = st.bid.active && st.bid.order_id == fill.order_id;
  const bool is_ask = st.ask.active && st.ask.order_id == fill.order_id;
  if (!is_bid && !is_ask) {
    return;
  }
  if (fill.side == mf::phase4::OrderSide::Buy) {
    st.inventory += static_cast<std::int64_t>(fill.qty);
  } else {
    st.inventory -= static_cast<std::int64_t>(fill.qty);
  }
  if (!fill.partial) {
    if (is_bid) {
      st.bid = {};
    }
    if (is_ask) {
      st.ask = {};
    }
  }
  st.has_center = false;
}

}  // namespace mf::research
