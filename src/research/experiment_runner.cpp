#include "mf/research/experiment_runner.hpp"

#include <cstring>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "mf/core/crc32.hpp"
#include "mf/phase3/feature_bridge.hpp"
#include "mf/research/simulation_clock.hpp"

namespace mf::research {

namespace {
template <typename T>
void hash_scalar(std::uint32_t& crc, const T& v) {
  crc = mf::core::crc32_update(crc, reinterpret_cast<const std::byte*>(&v), sizeof(T));
}

class ExperimentState final : public IEventConsumer,
                              public mf::phase3::IFeaturePublisher,
                              public IOrderIntentSink {
 public:
  explicit ExperimentState(const ExperimentConfig& cfg)
      : engine_(cfg.participation_cap),
        pnl_(cfg.pnl),
        strategy_(this, cfg.strategy),
        ofi_strategy_(this, cfg.ofi_strategy),
        strategy_kind_(cfg.strategy_kind),
        bridge_(this) {
    engine_.set_fill_callback([this](const mf::phase4::Fill& fill) {
      const auto it = order_to_symbol_.find(fill.order_id);
      if (it == order_to_symbol_.end()) {
        return;
      }
      const auto role_it = order_taker_.find(fill.order_id);
      const bool taker = (role_it != order_taker_.end()) ? role_it->second : false;
      const double realized_before = pnl_.finalize().total_realized_pnl;
      pnl_.on_fill(it->second, fill, taker);
      strategy_.on_fill(it->second, fill);
      ofi_strategy_.on_fill(it->second, fill);
      ++fills_;
      const double realized_after = pnl_.finalize().total_realized_pnl;
      trade_pnls_.push_back(realized_after - realized_before);
      hash_scalar(output_hash_, fill.order_id);
      hash_scalar(output_hash_, fill.price);
      hash_scalar(output_hash_, fill.qty);
      hash_scalar(output_hash_, fill.ts_ns);
      if (!fill.partial) {
        order_to_symbol_.erase(it);
      }
    });
  }

  void on_event(const mf::core::BookEvent& ev) override {
    ++events_;
    if (!clock_.on_event(ev)) {
      ++clock_rejects_;
      return;
    }
    bridge_.on_merged_event(ev);
    if (strategy_kind_ == ExperimentConfig::StrategyKind::Ofi) {
      ofi_strategy_.on_event(ev);
    }
    engine_.on_market_event(ev);
    pnl_.on_tick_end(ev.exchange_ts_ns);
    equity_curve_.push_back(pnl_.finalize().total_equity);
    hash_scalar(output_hash_, ev.exchange_ts_ns);
  }

  bool try_publish(const mf::phase3::FeatureVector& fv) noexcept override {
    pnl_.on_feature(fv);
    by_symbol_top_[fv.symbol_u64] = {fv.nbbo_bid_price, fv.nbbo_ask_price};
    if (strategy_kind_ == ExperimentConfig::StrategyKind::MarketMaking) {
      strategy_.on_feature(fv);
    }
    hash_scalar(output_hash_, fv.symbol_u64);
    hash_scalar(output_hash_, fv.exchange_ts_ns);
    hash_scalar(output_hash_, fv.microprice);
    hash_scalar(output_hash_, fv.ofi);
    const double eq = pnl_.finalize().total_equity;
    if (has_last_equity_) {
      returns_.push_back(eq - last_equity_);
    }
    last_equity_ = eq;
    has_last_equity_ = true;
    return true;
  }

  void on_intent(const OrderIntent& intent) override {
    hash_scalar(output_hash_, intent.order_id);
    hash_scalar(output_hash_, intent.price);
    hash_scalar(output_hash_, intent.qty);
    hash_scalar(output_hash_, intent.ts_ns);
    const auto action = static_cast<std::uint8_t>(intent.action);
    hash_scalar(output_hash_, action);

    if (intent.action == OrderAction::Cancel) {
      ++cancel_intents_;
      engine_.cancel(intent.order_id, intent.ts_ns);
      order_to_symbol_.erase(intent.order_id);
      return;
    }

    ++submitted_orders_;
    order_to_symbol_[intent.order_id] = intent.symbol_u64;
    if (by_symbol_top_.find(intent.symbol_u64) != by_symbol_top_.end()) {
      const auto [bid, ask] = by_symbol_top_[intent.symbol_u64];
      const bool crosses =
          (intent.side == mf::phase4::OrderSide::Buy && ask > 0 && intent.price >= ask) ||
          (intent.side == mf::phase4::OrderSide::Sell && bid > 0 && intent.price <= bid);
      order_taker_[intent.order_id] = crosses;
    } else {
      order_taker_[intent.order_id] = false;
    }
    engine_.bind_order_symbol(intent.order_id, intent.symbol_u64);
    pnl_.on_order_submitted();
    engine_.submit(intent.side, intent.price, intent.qty, intent.order_id, intent.ts_ns);
  }

  void fill_report(ExperimentReport& out) const {
    out.events_replayed = events_;
    out.clock_rejects = clock_rejects_;
    out.submitted_orders = submitted_orders_;
    out.cancel_intents = cancel_intents_;
    out.fills = fills_;
    out.output_hash = output_hash_;
    out.pnl = pnl_.finalize();
    RiskMetricsInput rm{};
    rm.returns = returns_;
    rm.equity_curve = equity_curve_;
    rm.trade_pnls = trade_pnls_;
    rm.orders_submitted = submitted_orders_;
    rm.fills = fills_;
    rm.inventory_turnover = out.pnl.turnover;
    rm.avg_holding_time_ms = 0.0;
    out.risk = compute_risk_metrics(rm);
    out.period_pnls = returns_;
    out.trade_pnls = trade_pnls_;
    out.equity_curve = equity_curve_;
  }

 private:
  SimulationClock clock_{};
  mf::phase4::SimMatchingEngine engine_;
  mf::phase4::PnlAccountant pnl_;
  StrategyEngine strategy_;
  OfiStrategy ofi_strategy_;
  ExperimentConfig::StrategyKind strategy_kind_;
  mf::phase3::FeatureBridge bridge_;
  std::unordered_map<std::uint64_t, std::uint64_t> order_to_symbol_{};
  std::unordered_map<std::uint64_t, bool> order_taker_{};
  std::unordered_map<std::uint64_t, std::pair<std::uint32_t, std::uint32_t>> by_symbol_top_{};
  std::uint64_t events_{0};
  std::uint64_t clock_rejects_{0};
  std::uint64_t submitted_orders_{0};
  std::uint64_t cancel_intents_{0};
  std::uint64_t fills_{0};
  std::uint32_t output_hash_{0};
  std::vector<double> returns_{};
  std::vector<double> equity_curve_{};
  std::vector<double> trade_pnls_{};
  double last_equity_{0.0};
  bool has_last_equity_{false};
};

std::uint32_t config_hash(const ExperimentConfig& cfg) {
  std::uint32_t crc = 0;
  hash_scalar(crc, cfg.strategy.half_spread_ticks);
  hash_scalar(crc, cfg.strategy.quote_size);
  hash_scalar(crc, cfg.strategy.max_inventory);
  hash_scalar(crc, cfg.strategy.inventory_skew_ticks_per_unit);
  hash_scalar(crc, cfg.strategy.ofi_skew_coef);
  hash_scalar(crc, cfg.strategy.requote_threshold_ticks);
  hash_scalar(crc, cfg.strategy.cancel_replace_cooldown_ns);
  hash_scalar(crc, static_cast<std::uint8_t>(cfg.strategy_kind));
  hash_scalar(crc, cfg.ofi_strategy.threshold);
  hash_scalar(crc, cfg.ofi_strategy.max_position);
  hash_scalar(crc, cfg.ofi_strategy.quote_size);
  hash_scalar(crc, cfg.ofi_strategy.half_spread_ticks);
  hash_scalar(crc, cfg.ofi_strategy.requote_cooldown_events);
  hash_scalar(crc, cfg.pnl.maker_rebate_per_share);
  hash_scalar(crc, cfg.pnl.taker_fee_per_share);
  hash_scalar(crc, cfg.pnl.sharpe_bucket_ns);
  hash_scalar(crc, cfg.participation_cap);
  return crc;
}
}  // namespace

ExperimentRunner::ExperimentRunner() : ExperimentRunner(ExperimentConfig{}) {}

ExperimentRunner::ExperimentRunner(ExperimentConfig cfg) : cfg_(cfg) {}

bool ExperimentRunner::run_on_events(const std::span<const mf::core::BookEvent> events, ExperimentReport& report) {
  ExperimentState state(cfg_);
  for (const auto& ev : events) {
    state.on_event(ev);
  }
  state.fill_report(report);
  report.input.records = events.size();
  report.config_hash = config_hash(cfg_);
  return report.clock_rejects == 0;
}

bool ExperimentRunner::run(const EventStore& store, ExperimentReport& report) {
  ExperimentState state(cfg_);
  EventStoreStats stats{};
  const bool ok = store.replay(state, &stats);
  state.fill_report(report);
  report.input = stats;
  report.config_hash = config_hash(cfg_);
  return ok && report.clock_rejects == 0;
}

std::string experiment_report_to_json(const ExperimentReport& r, const std::string& journal_path) {
  std::ostringstream os;
  os << "{"
     << "\"journal_path\":\"" << journal_path << "\","
     << "\"records\":" << r.input.records << ","
     << "\"input_crc\":" << r.input.crc << ","
     << "\"crc_failures\":" << r.input.crc_failures << ","
     << "\"first_exchange_ts_ns\":" << r.input.first_exchange_ts_ns << ","
     << "\"last_exchange_ts_ns\":" << r.input.last_exchange_ts_ns << ","
     << "\"events_replayed\":" << r.events_replayed << ","
     << "\"clock_rejects\":" << r.clock_rejects << ","
     << "\"submitted_orders\":" << r.submitted_orders << ","
     << "\"cancel_intents\":" << r.cancel_intents << ","
     << "\"fills\":" << r.fills << ","
     << "\"config_hash\":" << r.config_hash << ","
     << "\"output_hash\":" << r.output_hash << ","
     << "\"risk\":{"
     << "\"sharpe_ratio\":" << r.risk.sharpe_ratio << ","
     << "\"sortino_ratio\":" << r.risk.sortino_ratio << ","
     << "\"max_drawdown\":" << r.risk.max_drawdown << ","
     << "\"win_rate\":" << r.risk.win_rate << ","
     << "\"avg_pnl_per_trade\":" << r.risk.avg_pnl_per_trade << ","
     << "\"fill_ratio\":" << r.risk.fill_ratio << ","
     << "\"inventory_turnover\":" << r.risk.inventory_turnover << ","
     << "\"avg_holding_time_ms\":" << r.risk.avg_holding_time_ms
     << "},"
     << "\"pnl\":{"
     << "\"realized\":" << r.pnl.total_realized_pnl << ","
     << "\"unrealized\":" << r.pnl.total_unrealized_pnl << ","
     << "\"equity\":" << r.pnl.total_equity << ","
     << "\"sharpe\":" << r.pnl.sharpe << ","
     << "\"max_drawdown\":" << r.pnl.max_drawdown << ","
     << "\"fill_ratio\":" << r.pnl.fill_ratio << ","
     << "\"turnover\":" << r.pnl.turnover << ","
     << "\"cumulative_fees\":" << r.pnl.cumulative_fees << ","
     << "\"cumulative_rebates\":" << r.pnl.cumulative_rebates
     << "}}";
  return os.str();
}

}  // namespace mf::research
