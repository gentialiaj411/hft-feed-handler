#include "mf/research/experiment_runner.hpp"

#include <cstring>
#include <sstream>
#include <unordered_map>

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
        bridge_(this) {
    engine_.set_fill_callback([this](const mf::phase4::Fill& fill) {
      const auto it = order_to_symbol_.find(fill.order_id);
      if (it == order_to_symbol_.end()) {
        return;
      }
      pnl_.on_fill(it->second, fill);
      strategy_.on_fill(it->second, fill);
      ++fills_;
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
    engine_.on_market_event(ev);
    pnl_.on_tick_end(ev.exchange_ts_ns);
    hash_scalar(output_hash_, ev.exchange_ts_ns);
  }

  bool try_publish(const mf::phase3::FeatureVector& fv) noexcept override {
    pnl_.on_feature(fv);
    strategy_.on_feature(fv);
    hash_scalar(output_hash_, fv.symbol_u64);
    hash_scalar(output_hash_, fv.exchange_ts_ns);
    hash_scalar(output_hash_, fv.microprice);
    hash_scalar(output_hash_, fv.ofi);
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
  }

 private:
  SimulationClock clock_{};
  mf::phase4::SimMatchingEngine engine_;
  mf::phase4::PnlAccountant pnl_;
  StrategyEngine strategy_;
  mf::phase3::FeatureBridge bridge_;
  std::unordered_map<std::uint64_t, std::uint64_t> order_to_symbol_{};
  std::uint64_t events_{0};
  std::uint64_t clock_rejects_{0};
  std::uint64_t submitted_orders_{0};
  std::uint64_t cancel_intents_{0};
  std::uint64_t fills_{0};
  std::uint32_t output_hash_{0};
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
  hash_scalar(crc, cfg.pnl.sharpe_bucket_ns);
  hash_scalar(crc, cfg.participation_cap);
  return crc;
}
}  // namespace

ExperimentRunner::ExperimentRunner() : ExperimentRunner(ExperimentConfig{}) {}

ExperimentRunner::ExperimentRunner(ExperimentConfig cfg) : cfg_(cfg) {}

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
     << "\"pnl\":{"
     << "\"realized\":" << r.pnl.total_realized_pnl << ","
     << "\"unrealized\":" << r.pnl.total_unrealized_pnl << ","
     << "\"equity\":" << r.pnl.total_equity << ","
     << "\"sharpe\":" << r.pnl.sharpe << ","
     << "\"max_drawdown\":" << r.pnl.max_drawdown << ","
     << "\"fill_ratio\":" << r.pnl.fill_ratio << ","
     << "\"turnover\":" << r.pnl.turnover
     << "}}";
  return os.str();
}

}  // namespace mf::research
