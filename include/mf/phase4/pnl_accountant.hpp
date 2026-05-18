#pragma once

#include <cstdint>
#include <unordered_map>

#include "mf/phase3/types.hpp"
#include "mf/phase4/sim_matching_engine.hpp"

namespace mf::phase4 {

class PnlAccountant {
 public:
  struct Config {
    std::uint64_t sharpe_bucket_ns{1'000'000'000ULL};
  };

  struct SymbolStats {
    std::int64_t position{0};
    double cash{0.0};
    double realized_pnl{0.0};
    double unrealized_pnl{0.0};
    std::uint64_t fill_count{0};
    double gross_volume{0.0};
  };

  struct Report {
    double total_realized_pnl{0.0};
    double total_unrealized_pnl{0.0};
    double total_equity{0.0};
    double max_drawdown{0.0};
    double sharpe{0.0};
    double fill_ratio{0.0};
    double turnover{0.0};
    std::uint64_t fills{0};
    std::uint64_t submitted_orders{0};
  };

  PnlAccountant();
  explicit PnlAccountant(Config cfg);

  void on_fill(std::uint64_t symbol_u64, const Fill& fill);
  void on_feature(const mf::phase3::FeatureVector& fv);
  void on_order_submitted();
  void on_tick_end(std::uint64_t ts_ns);
  Report finalize() const;

 private:
  struct SymbolState {
    std::int64_t position{0};
    double cash{0.0};
    double avg_cost{0.0};
    double realized_pnl{0.0};
    double unrealized_pnl{0.0};
    std::uint64_t fill_count{0};
    double gross_volume{0.0};
    double last_mid{0.0};
    bool has_mid{false};
  };

  Config cfg_{};
  std::unordered_map<std::uint64_t, SymbolState> by_symbol_{};
  std::uint64_t submitted_orders_{0};
  std::uint64_t total_fills_{0};

  double peak_equity_{0.0};
  double max_drawdown_{0.0};
  std::uint64_t current_bucket_{0};
  bool has_bucket_{false};
  double last_bucket_equity_{0.0};
  double sum_returns_{0.0};
  double sum_sq_returns_{0.0};
  std::uint64_t n_returns_{0};
};

}  // namespace mf::phase4
