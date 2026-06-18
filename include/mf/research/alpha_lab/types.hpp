#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "mf/phase3/types.hpp"

namespace mf::research::alpha_lab {

inline constexpr double kMissingLabel = std::numeric_limits<double>::quiet_NaN();

struct LabelHorizons {
  std::vector<std::uint64_t> horizons_ns{1'000'000'000ULL, 5'000'000'000ULL, 10'000'000'000ULL};
};

struct MidObservation {
  std::uint64_t symbol_u64{0};
  std::uint64_t exchange_ts_ns{0};
  double mid{0.0};
};

struct LabeledFeatureRow {
  std::uint64_t symbol_u64{0};
  std::uint64_t exchange_ts_ns{0};
  std::uint64_t ingest_ts_ns{0};
  std::uint32_t nbbo_bid_price{0};
  std::uint32_t nbbo_bid_qty{0};
  std::uint32_t nbbo_ask_price{0};
  std::uint32_t nbbo_ask_qty{0};
  double mid{0.0};
  double microprice{0.0};
  double ofi{0.0};
  double queue_ahead{0.0};
  double effective_spread{0.0};
  double kyle_lambda{0.0};
  double vpin{0.0};
  std::vector<double> forward_mid_returns{};
};

struct MaterializeStats {
  std::uint64_t journal_events{0};
  std::uint64_t feature_rows{0};
  std::uint32_t journal_crc{0};
  std::uint64_t first_exchange_ts_ns{0};
  std::uint64_t last_exchange_ts_ns{0};
  double wall_seconds{0.0};
};

struct AblationRow {
  std::string variant{};
  double mean_ic{0.0};
  double mean_quintile_spread{0.0};
  double net_sharpe{0.0};
};

[[nodiscard]] inline double mid_from_nbbo(std::uint32_t bid, std::uint32_t ask) noexcept {
  if (bid == 0 || ask == 0) {
    return 0.0;
  }
  return 0.5 * (static_cast<double>(bid) + static_cast<double>(ask));
}

[[nodiscard]] inline LabeledFeatureRow from_feature_vector(
    const mf::phase3::FeatureVector& fv,
    std::size_t label_count) {
  LabeledFeatureRow row{};
  row.symbol_u64 = fv.symbol_u64;
  row.exchange_ts_ns = fv.exchange_ts_ns;
  row.ingest_ts_ns = fv.ingest_ts_ns;
  row.nbbo_bid_price = fv.nbbo_bid_price;
  row.nbbo_bid_qty = fv.nbbo_bid_qty;
  row.nbbo_ask_price = fv.nbbo_ask_price;
  row.nbbo_ask_qty = fv.nbbo_ask_qty;
  row.mid = mid_from_nbbo(fv.nbbo_bid_price, fv.nbbo_ask_price);
  row.microprice = fv.microprice;
  row.ofi = fv.ofi;
  row.queue_ahead = fv.queue_ahead;
  row.effective_spread = fv.effective_spread;
  row.kyle_lambda = fv.kyle_lambda;
  row.vpin = fv.vpin;
  row.forward_mid_returns.assign(label_count, kMissingLabel);
  return row;
}

}  // namespace mf::research::alpha_lab
