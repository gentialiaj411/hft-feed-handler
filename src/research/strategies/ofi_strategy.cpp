#include "mf/research/strategies/ofi_strategy.hpp"

#include <algorithm>

namespace mf::research {

OfiStrategy::OfiStrategy(IOrderIntentSink* sink, Config cfg) : sink_(sink), cfg_(cfg) {}

void OfiStrategy::cancel_if_active(
    std::uint64_t symbol_u64,
    std::uint64_t& order_id,
    mf::phase4::OrderSide side,
    std::int64_t price,
    std::uint64_t ts_ns) {
  if (sink_ == nullptr || order_id == 0) {
    return;
  }
  sink_->on_intent(OrderIntent{
      OrderAction::Cancel,
      symbol_u64,
      side,
      price,
      0,
      ts_ns,
      order_id});
  order_id = 0;
}

std::uint64_t OfiStrategy::submit(
    std::uint64_t symbol_u64,
    mf::phase4::OrderSide side,
    std::int64_t price,
    std::uint64_t qty,
    std::uint64_t ts_ns) {
  if (sink_ == nullptr || qty == 0) {
    return 0;
  }
  const std::uint64_t order_id = next_order_id_++;
  sink_->on_intent(OrderIntent{
      OrderAction::Submit,
      symbol_u64,
      side,
      price,
      qty,
      ts_ns,
      order_id});
  return order_id;
}

void OfiStrategy::on_event(const mf::core::BookEvent& ev) {
  const std::uint64_t symbol = ev.symbol.as_u64();
  auto& st = by_symbol_[symbol];
  if (st.signal.value() == 0.0 && st.inventory == 0 &&
      st.bid_order_id == 0 && st.ask_order_id == 0 && !st.has_last_quote) {
    st.signal = OfiSignal(cfg_.signal);
  }

  // Maintain a real order book so we know the actual best bid / best ask.
  // The previous version tracked `bid_px = ev.price` on every buy-side event and
  // `ask_px = ev.price` on every sell-side event, which is the last-seen event
  // price by side, not the top of book. The mid computed from those was
  // approximately uncorrelated with the true mid, so quotes landed far from the
  // market and produced a ~0.01% fill rate. (See AUDIT_LOG 2026-05-25.)
  (void)book_engine_.on_event(ev);
  st.signal.update(ev);

  const auto top = book_engine_.top_of_book(ev.venue, symbol);
  if (!top.has_bid || !top.has_ask) {
    return;
  }

  ++st.events_since_quote;

  const double ofi = st.signal.value();
  const std::int64_t best_bid = static_cast<std::int64_t>(top.bid_price);
  const std::int64_t best_ask = static_cast<std::int64_t>(top.ask_price);
  const std::int64_t mid = (best_bid + best_ask) / 2;

  // Original quoting semantics: quote at center ± half_spread_ticks where center
  // shifts to best_ask on strong positive OFI and to best_bid on strong negative.
  std::int64_t center = mid;
  if (ofi > cfg_.threshold) {
    center = std::max<std::int64_t>(mid, best_ask);
  } else if (ofi < -cfg_.threshold) {
    center = std::min<std::int64_t>(mid, best_bid);
  }
  const std::int64_t bid_quote = center - cfg_.half_spread_ticks;
  const std::int64_t ask_quote = center + cfg_.half_spread_ticks;

  if (st.has_last_quote && st.bid_order_id != 0 && st.ask_order_id != 0 &&
      st.last_bid_quote == bid_quote && st.last_ask_quote == ask_quote) {
    return;
  }
  if (cfg_.requote_cooldown_events > 0 &&
      st.bid_order_id != 0 && st.ask_order_id != 0 &&
      st.events_since_quote < cfg_.requote_cooldown_events) {
    return;
  }

  cancel_if_active(symbol, st.bid_order_id, mf::phase4::OrderSide::Buy, st.last_bid_quote, ev.exchange_ts_ns);
  cancel_if_active(symbol, st.ask_order_id, mf::phase4::OrderSide::Sell, st.last_ask_quote, ev.exchange_ts_ns);

  if (st.inventory >= cfg_.max_position) {
    const std::int64_t px = best_bid - cfg_.aggressive_flatten_offset_ticks;
    st.ask_order_id = submit(symbol, mf::phase4::OrderSide::Sell, px, cfg_.quote_size, ev.exchange_ts_ns);
    st.last_ask_quote = px;
    st.has_last_quote = true;
    st.events_since_quote = 0;
    return;
  }
  if (st.inventory <= -cfg_.max_position) {
    const std::int64_t px = best_ask + cfg_.aggressive_flatten_offset_ticks;
    st.bid_order_id = submit(symbol, mf::phase4::OrderSide::Buy, px, cfg_.quote_size, ev.exchange_ts_ns);
    st.last_bid_quote = px;
    st.has_last_quote = true;
    st.events_since_quote = 0;
    return;
  }

  st.bid_order_id = submit(symbol, mf::phase4::OrderSide::Buy, bid_quote, cfg_.quote_size, ev.exchange_ts_ns);
  st.ask_order_id = submit(symbol, mf::phase4::OrderSide::Sell, ask_quote, cfg_.quote_size, ev.exchange_ts_ns);
  st.last_bid_quote = bid_quote;
  st.last_ask_quote = ask_quote;
  st.has_last_quote = true;
  st.events_since_quote = 0;
}

void OfiStrategy::on_fill(std::uint64_t symbol_u64, const mf::phase4::Fill& fill) {
  auto it = by_symbol_.find(symbol_u64);
  if (it == by_symbol_.end()) {
    return;
  }
  auto& st = it->second;
  if (fill.side == mf::phase4::OrderSide::Buy) {
    st.inventory += static_cast<std::int64_t>(fill.qty);
  } else {
    st.inventory -= static_cast<std::int64_t>(fill.qty);
  }
  if (!fill.partial) {
    if (st.bid_order_id == fill.order_id) st.bid_order_id = 0;
    if (st.ask_order_id == fill.order_id) st.ask_order_id = 0;
  }
}

}  // namespace mf::research
