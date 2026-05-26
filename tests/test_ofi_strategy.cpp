#include <cassert>
#include <vector>

#include "mf/research/strategies/ofi_strategy.hpp"

namespace {
struct Sink final : mf::research::IOrderIntentSink {
  std::vector<mf::research::OrderIntent> intents{};
  void on_intent(const mf::research::OrderIntent& i) override { intents.push_back(i); }
};

mf::core::BookEvent add_order(mf::core::Side side, std::uint32_t px, std::uint32_t qty,
                              std::uint64_t order_id, std::uint64_t ts) {
  mf::core::BookEvent e{};
  e.symbol.bytes = {'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '};
  e.venue = mf::core::Venue::Nasdaq;
  e.type = mf::core::EventType::Add;
  e.side = side;
  e.price = px;
  e.qty = qty;
  e.order_id = order_id;
  e.sequence = order_id;
  e.exchange_ts_ns = ts;
  e.raw_type = 'A';
  return e;
}

std::size_t count_submits(const std::vector<mf::research::OrderIntent>& intents) {
  std::size_t n = 0;
  for (const auto& i : intents) {
    if (i.action == mf::research::OrderAction::Submit) ++n;
  }
  return n;
}
}  // namespace

int main() {
  Sink sink;
  mf::research::OfiStrategy::Config cfg{};
  cfg.threshold = 3.0;
  cfg.max_position = 5;
  cfg.quote_size = 2;
  cfg.half_spread_ticks = 1;
  cfg.requote_cooldown_events = 0;
  mf::research::OfiStrategy strat(&sink, cfg);

  // Build a real top of book with Add events: best bid 100 (qty 10) and best ask 110 (qty 10).
  // Spread is 10 ticks so half_spread_ticks=1 lands the strategy comfortably inside the spread.
  strat.on_event(add_order(mf::core::Side::Buy, 100, 10, 1, 1));
  assert(sink.intents.empty());

  strat.on_event(add_order(mf::core::Side::Sell, 110, 10, 2, 2));
  // mid = 105. Default branch: bid_quote = 104, ask_quote = 106.
  assert(sink.intents.size() == 2);
  assert(sink.intents[0].action == mf::research::OrderAction::Submit);
  assert(sink.intents[0].side == mf::phase4::OrderSide::Buy);
  assert(sink.intents[0].price == 104);
  assert(sink.intents[1].action == mf::research::OrderAction::Submit);
  assert(sink.intents[1].side == mf::phase4::OrderSide::Sell);
  assert(sink.intents[1].price == 106);

  // Strong positive OFI: a fresh aggressive bid event with large qty drives the signal
  // above the threshold. center should shift to best_ask (110), giving bid@109, ask@111.
  strat.on_event(add_order(mf::core::Side::Buy, 109, 50, 3, 3));
  // best bid is now 109, best ask is still 110, mid = 109. OFI > threshold => center = ask = 110.
  // Quotes: bid = 109, ask = 111.
  const std::size_t submits_after = count_submits(sink.intents);
  assert(submits_after >= 3);
  // Should have cancelled both prior quotes and resubmitted.
  bool saw_cancel_bid = false;
  bool saw_cancel_ask = false;
  bool saw_new_bid = false;
  bool saw_new_ask = false;
  for (std::size_t i = 2; i < sink.intents.size(); ++i) {
    const auto& it = sink.intents[i];
    if (it.action == mf::research::OrderAction::Cancel) {
      if (it.side == mf::phase4::OrderSide::Buy) saw_cancel_bid = true;
      if (it.side == mf::phase4::OrderSide::Sell) saw_cancel_ask = true;
    } else if (it.action == mf::research::OrderAction::Submit) {
      if (it.side == mf::phase4::OrderSide::Buy && it.price == 109) saw_new_bid = true;
      if (it.side == mf::phase4::OrderSide::Sell && it.price == 111) saw_new_ask = true;
    }
  }
  assert(saw_cancel_bid);
  assert(saw_cancel_ask);
  assert(saw_new_bid);
  assert(saw_new_ask);

  return 0;
}
