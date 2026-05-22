#include <cassert>
#include <vector>

#include "mf/research/strategy_engine.hpp"

namespace {
struct CollectSink final : mf::research::IOrderIntentSink {
  std::vector<mf::research::OrderIntent> intents{};
  void on_intent(const mf::research::OrderIntent& intent) override { intents.push_back(intent); }
};
}  // namespace

int main() {
  CollectSink sink;
  mf::research::StrategyEngine::Config cfg{};
  cfg.half_spread_ticks = 2;
  cfg.quote_size = 5;
  cfg.requote_threshold_ticks = 1;
  mf::research::StrategyEngine engine(&sink, cfg);

  mf::phase3::FeatureVector fv{};
  fv.symbol_u64 = 0x4141504C;
  fv.exchange_ts_ns = 100;
  fv.microprice = 101.0;
  engine.on_feature(fv);
  assert(sink.intents.size() == 2);
  assert(sink.intents[0].action == mf::research::OrderAction::Submit);
  assert(sink.intents[0].side == mf::phase4::OrderSide::Buy);
  assert(sink.intents[0].price == 99);
  assert(sink.intents[0].qty == 5);
  assert(sink.intents[1].side == mf::phase4::OrderSide::Sell);
  assert(sink.intents[1].price == 103);

  fv.exchange_ts_ns = 101;
  fv.microprice = 101.2;
  engine.on_feature(fv);
  assert(sink.intents.size() == 2);

  fv.exchange_ts_ns = 102;
  fv.microprice = 103.0;
  engine.on_feature(fv);
  assert(sink.intents.size() == 6);
  assert(sink.intents[2].action == mf::research::OrderAction::Cancel);
  assert(sink.intents[3].action == mf::research::OrderAction::Cancel);
  assert(sink.intents[4].action == mf::research::OrderAction::Submit);
  assert(sink.intents[5].action == mf::research::OrderAction::Submit);

  mf::phase4::Fill fill{};
  fill.order_id = sink.intents[4].order_id;
  fill.side = mf::phase4::OrderSide::Buy;
  fill.qty = 3;
  fill.partial = false;
  engine.on_fill(fv.symbol_u64, fill);

  fv.exchange_ts_ns = 103;
  engine.on_feature(fv);
  assert(sink.intents.size() == 9);
  assert(sink.intents[6].action == mf::research::OrderAction::Cancel);
  assert(sink.intents[6].order_id == sink.intents[5].order_id);
  assert(sink.intents[7].action == mf::research::OrderAction::Submit);
  assert(sink.intents[7].side == mf::phase4::OrderSide::Buy);
  assert(sink.intents[8].action == mf::research::OrderAction::Submit);
  assert(sink.intents[8].side == mf::phase4::OrderSide::Sell);
  return 0;
}
