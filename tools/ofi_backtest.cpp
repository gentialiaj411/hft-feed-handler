#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "mf/research/event_store.hpp"
#include "mf/research/experiment_runner.hpp"
#include "mf/research/metrics/bootstrap.hpp"

namespace {
std::string arg(int argc, char** argv, const std::string& key, const std::string& dflt) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] == key) return argv[i + 1];
  }
  return dflt;
}

std::uint64_t arg_u64(int argc, char** argv, const std::string& key, std::uint64_t dflt) {
  const std::string v = arg(argc, argv, key, "");
  if (v.empty()) return dflt;
  return static_cast<std::uint64_t>(std::stoull(v));
}

double arg_f64(int argc, char** argv, const std::string& key, double dflt) {
  const std::string v = arg(argc, argv, key, "");
  if (v.empty()) return dflt;
  return std::stod(v);
}

double annualized_sharpe_from_periods(const std::vector<double>& period_pnls, std::uint64_t bucket_ns) {
  if (period_pnls.size() < 2) {
    return 0.0;
  }
  double sum = 0.0;
  double sum_sq = 0.0;
  for (const double p : period_pnls) {
    sum += p;
    sum_sq += p * p;
  }
  const double n = static_cast<double>(period_pnls.size());
  const double mean = sum / n;
  const double var = (sum_sq / n) - (mean * mean);
  if (var <= 0.0) {
    return 0.0;
  }
  const double periods_per_year =
      (bucket_ns > 0) ? (31'536'000'000'000'000ULL / static_cast<double>(bucket_ns)) : n;
  return mean / std::sqrt(var) * std::sqrt(periods_per_year);
}
}  // namespace

int main(int argc, char** argv) {
#if !defined(__linux__)
  std::printf("ofi_backtest is Linux-only\n");
  return 0;
#else
  const std::string journal = arg(argc, argv, "--journal", "");
  const std::string out_md = arg(argc, argv, "--out-md", "bench/results/ofi_backtest.md");
  const std::string out_csv = arg(argc, argv, "--out-csv", "bench/results/ofi_backtest_raw.csv");
  const double holdout_frac = arg_f64(argc, argv, "--holdout-frac", 0.30);
  const std::uint64_t bootstrap_iters = arg_u64(argc, argv, "--bootstrap-iters", 2000);
  const std::uint64_t bootstrap_block = arg_u64(argc, argv, "--bootstrap-block", 0);
  const std::uint64_t seed = arg_u64(argc, argv, "--seed", 42);
  if (journal.empty()) {
    std::fprintf(stderr,
        "usage: ofi_backtest --journal <path> [--holdout-frac 0.30] [--out-md path] [--out-csv path]\n");
    return 2;
  }

  mf::research::EventStore store(journal);
  auto all = store.load_all();
  if (all.empty()) {
    std::fprintf(stderr, "empty journal\n");
    return 1;
  }
  std::sort(all.begin(), all.end(), [](const mf::core::BookEvent& a, const mf::core::BookEvent& b) {
    if (a.exchange_ts_ns != b.exchange_ts_ns) return a.exchange_ts_ns < b.exchange_ts_ns;
    if (a.venue != b.venue) return static_cast<std::uint8_t>(a.venue) < static_cast<std::uint8_t>(b.venue);
    return a.sequence < b.sequence;
  });

  const std::uint64_t t_min = all.front().exchange_ts_ns;
  const std::uint64_t t_max = all.back().exchange_ts_ns;
  const std::uint64_t holdout_start =
      t_min + static_cast<std::uint64_t>(static_cast<double>(t_max - t_min) * (1.0 - holdout_frac));

  std::vector<mf::core::BookEvent> holdout;
  holdout.reserve(all.size() / 3 + 1);
  for (const auto& ev : all) {
    if (ev.exchange_ts_ns >= holdout_start) {
      holdout.push_back(ev);
    }
  }
  if (holdout.empty()) {
    std::fprintf(stderr, "holdout window empty\n");
    return 1;
  }

  mf::research::ExperimentConfig cfg{};
  cfg.strategy_kind = mf::research::ExperimentConfig::StrategyKind::Ofi;
  cfg.ofi_strategy.threshold = arg_f64(argc, argv, "--ofi-threshold", 100.0);
  cfg.ofi_strategy.max_position = static_cast<std::int64_t>(arg_u64(argc, argv, "--max-position", 200));
  cfg.ofi_strategy.quote_size = arg_u64(argc, argv, "--quote-size", 20);
  cfg.pnl.maker_rebate_per_share = arg_f64(argc, argv, "--maker-rebate", 0.0020);
  cfg.pnl.taker_fee_per_share = arg_f64(argc, argv, "--taker-fee", 0.0030);
  cfg.pnl.sharpe_bucket_ns = arg_u64(argc, argv, "--sharpe-bucket-ns", cfg.pnl.sharpe_bucket_ns);

  mf::research::ExperimentRunner runner(cfg);
  mf::research::ExperimentReport report{};
  if (!runner.run_on_events(holdout, report)) {
    std::fprintf(stderr, "experiment failed clock_rejects=%llu\n",
        static_cast<unsigned long long>(report.clock_rejects));
    return 1;
  }

  std::vector<double> equity_deltas;
  equity_deltas.reserve(report.equity_curve.size());
  for (std::size_t i = 1; i < report.equity_curve.size(); ++i) {
    const double d = report.equity_curve[i] - report.equity_curve[i - 1];
    if (d != 0.0) {
      equity_deltas.push_back(d);
    }
  }

  const std::vector<double>& pnl_series = !equity_deltas.empty()
      ? equity_deltas
      : (!report.trade_pnls.empty() ? report.trade_pnls : report.period_pnls);
  const std::uint64_t block_size = bootstrap_block > 0
      ? bootstrap_block
      : std::max<std::uint64_t>(1, pnl_series.size() / 10);
  const auto boot = mf::research::block_bootstrap_mean_pvalue(pnl_series, block_size, bootstrap_iters, seed);
  const double sharpe_ann = report.pnl.sharpe;

  {
    std::ofstream csv(out_csv);
    csv << "series,period_index,pnl\n";
    for (std::size_t i = 0; i < report.trade_pnls.size(); ++i) {
      csv << "trade," << i << "," << report.trade_pnls[i] << "\n";
    }
    for (std::size_t i = 0; i < report.period_pnls.size(); ++i) {
      csv << "bucket," << i << "," << report.period_pnls[i] << "\n";
    }
  }

  {
    std::ofstream md(out_md);
    md << "# OFI backtest (offline replay, simulated fills)\n\n";
    md << "**CAVEAT:** This is an offline replay backtest with simulated maker/taker fills. ";
    md << "It is not live trading evidence.\n\n";
    md << "## Held-out split\n";
    md << "- Journal: `" << journal << "`\n";
    md << "- Exchange-time holdout: last " << (holdout_frac * 100.0) << "% of `[t_min, t_max]`\n";
    md << "- `holdout_start_ns=" << holdout_start << "` (`t_min=" << t_min << "`, `t_max=" << t_max << "`)\n";
    md << "- Holdout events: " << holdout.size() << " / " << all.size() << "\n\n";
    md << "## Strategy config (fixed)\n";
    md << "- OFI threshold: " << cfg.ofi_strategy.threshold << "\n";
    md << "- max_position: " << cfg.ofi_strategy.max_position << "\n";
    md << "- quote_size: " << cfg.ofi_strategy.quote_size << "\n";
    md << "- maker_rebate/share: " << cfg.pnl.maker_rebate_per_share << "\n";
    md << "- taker_fee/share: " << cfg.pnl.taker_fee_per_share << "\n";
    md << "- sharpe_bucket_ns: " << cfg.pnl.sharpe_bucket_ns << "\n\n";
    md << "## Results\n";
    md << "| metric | value |\n|---|---|\n";
    md << "| fills | " << report.fills << " |\n";
    md << "| submitted_orders | " << report.submitted_orders << " |\n";
    md << "| hit_rate | " << report.risk.win_rate << " |\n";
    md << "| avg_pnl_per_trade | " << report.risk.avg_pnl_per_trade << " |\n";
    md << "| max_drawdown | " << report.risk.max_drawdown << " |\n";
    md << "| sharpe_annualized | " << sharpe_ann << " |\n";
    md << "| block_bootstrap_p_value | " << boot.p_value_greater_than_zero << " |\n";
    md << "| bootstrap_block_size | " << boot.block_size << " |\n";
    md << "| bootstrap_iterations | " << boot.iterations << " |\n";
    md << "\nSharpe annualization: trade-level series uses `sqrt(252)` scaling on per-fill realized PnL; ";
    md << "bucket fallback uses `sharpe_bucket_ns` from config.\n";
    md << "- pnl_series_for_bootstrap: "
       << (equity_deltas.empty() ? (report.trade_pnls.empty() ? "bucket_returns" : "trade_pnls") : "equity_deltas")
       << " (n=" << pnl_series.size() << ")\n";
    md << "- accountant_sharpe (1s buckets): " << report.pnl.sharpe << "\n";
    md << "\nRaw series: `" << out_csv << "`\n";
  }

  std::printf(
      "holdout_events=%zu sharpe_ann=%.6f max_dd=%.2f hit_rate=%.4f fills=%llu bootstrap_p=%.4f\n",
      holdout.size(),
      sharpe_ann,
      report.risk.max_drawdown,
      report.risk.win_rate,
      static_cast<unsigned long long>(report.fills),
      boot.p_value_greater_than_zero);
  return 0;
#endif
}
