#include "mf/research/alpha_lab/feature_store.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#include "mf/phase3/feature_bridge.hpp"
#include "mf/research/event_store.hpp"

namespace mf::research::alpha_lab {

namespace {

class RowCollector final : public mf::phase3::IFeaturePublisher {
 public:
  explicit RowCollector(std::size_t label_count) : label_count_(label_count) {}

  bool try_publish(const mf::phase3::FeatureVector& fv) noexcept override {
    rows_.push_back(from_feature_vector(fv, label_count_));
    return true;
  }

  std::vector<LabeledFeatureRow> rows_{};

 private:
  std::size_t label_count_{0};
};

class ReplayConsumer final : public mf::research::IEventConsumer {
 public:
  explicit ReplayConsumer(mf::phase3::FeatureBridge& bridge) : bridge_(bridge) {}

  void on_event(const mf::core::BookEvent& ev) override { bridge_.on_merged_event(ev); }

 private:
  mf::phase3::FeatureBridge& bridge_;
};

std::string csv_cell(double value) {
  if (std::isnan(value)) {
    return "";
  }
  std::ostringstream os;
  os << std::setprecision(17) << value;
  return os.str();
}

}  // namespace

FeatureStore::FeatureStore() = default;

FeatureStore::FeatureStore(Config cfg) : cfg_(cfg) {}

bool FeatureStore::materialize_from_journal(
    const std::string& journal_path,
    std::vector<LabeledFeatureRow>& out,
    MaterializeStats& stats) const {
  const auto start = std::chrono::steady_clock::now();
  const std::size_t label_count = cfg_.labels.horizons.horizons_ns.size();
  RowCollector collector(label_count);
  mf::phase3::FeatureBridge bridge(&collector, cfg_.pipeline);

  mf::research::EventStoreStats replay_stats{};
  ReplayConsumer consumer(bridge);
  mf::research::EventStore store(journal_path);
  if (!store.replay(consumer, &replay_stats)) {
    return false;
  }

  LabelBuilder label_builder(cfg_.labels);
  out = std::move(collector.rows_);
  label_builder.attach_labels(out);

  const auto end = std::chrono::steady_clock::now();
  stats.journal_events = replay_stats.records;
  stats.feature_rows = out.size();
  stats.journal_crc = replay_stats.crc;
  stats.first_exchange_ts_ns = replay_stats.first_exchange_ts_ns;
  stats.last_exchange_ts_ns = replay_stats.last_exchange_ts_ns;
  stats.wall_seconds =
      std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
  return replay_stats.crc_failures == 0;
}

bool FeatureStore::write_csv(const std::string& path, const std::vector<LabeledFeatureRow>& rows) const {
  std::ofstream out(path);
  if (!out) {
    return false;
  }

  out << "symbol_u64,exchange_ts_ns,ingest_ts_ns,mid,microprice,ofi,queue_ahead,effective_spread,"
         "kyle_lambda,vpin,nbbo_bid_price,nbbo_bid_qty,nbbo_ask_price,nbbo_ask_qty";
  for (std::size_t i = 0; i < cfg_.labels.horizons.horizons_ns.size(); ++i) {
    out << ",fwd_mid_ret_" << cfg_.labels.horizons.horizons_ns[i] << "ns";
  }
  out << "\n";

  for (const auto& row : rows) {
    out << row.symbol_u64 << ','
        << row.exchange_ts_ns << ','
        << row.ingest_ts_ns << ','
        << csv_cell(row.mid) << ','
        << csv_cell(row.microprice) << ','
        << csv_cell(row.ofi) << ','
        << csv_cell(row.queue_ahead) << ','
        << csv_cell(row.effective_spread) << ','
        << csv_cell(row.kyle_lambda) << ','
        << csv_cell(row.vpin) << ','
        << row.nbbo_bid_price << ','
        << row.nbbo_bid_qty << ','
        << row.nbbo_ask_price << ','
        << row.nbbo_ask_qty;
    for (double label : row.forward_mid_returns) {
      out << ',' << csv_cell(label);
    }
    out << "\n";
  }
  return true;
}

}  // namespace mf::research::alpha_lab
