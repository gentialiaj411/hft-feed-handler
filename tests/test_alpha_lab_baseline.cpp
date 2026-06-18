#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "mf/research/event_store.hpp"
#include "mf/research/experiment_runner.hpp"
#include "mf/research/metrics/bootstrap.hpp"

#if defined(__linux__)
#include <unistd.h>
#endif

namespace {

constexpr double kSharpeTol = 1e-4;
constexpr double kMetricTol = 1e-6;

struct BaselineExpectation {
  double sharpe_ann{0.10279};
  std::uint64_t fills{401};
  std::uint64_t submitted_orders{68370};
  std::uint32_t journal_crc{2369313900U};
  std::uint64_t holdout_start_ns{23085339771382ULL};
  std::uint64_t t_min{11039687760787ULL};
  std::uint64_t t_max{28247762061637ULL};
  std::uint64_t holdout_events{1843175};
  std::uint64_t total_events{5000000};
  double bootstrap_p_value{0.0};
};

bool near(double a, double b, double tol) {
  return std::fabs(a - b) <= tol;
}

std::string read_text(const std::string& path) {
  std::ifstream in(path);
  std::ostringstream os;
  os << in.rdbuf();
  return os.str();
}

}  // namespace

int main() {
#if !defined(__linux__)
  std::puts("SKIP: alpha lab baseline test is Linux-only");
  return 0;
#else
  const std::string journal = "bench/data/itch_20190130_5m_for_ofi.journal";
  const BaselineExpectation expected{};

  mf::research::EventStore store(journal);
  mf::research::EventStoreStats input_stats{};
  auto all = store.load_all(&input_stats);
  assert(!all.empty());
  assert(input_stats.crc == expected.journal_crc);

  std::sort(all.begin(), all.end(), [](const mf::core::BookEvent& a, const mf::core::BookEvent& b) {
    if (a.exchange_ts_ns != b.exchange_ts_ns) {
      return a.exchange_ts_ns < b.exchange_ts_ns;
    }
    if (a.venue != b.venue) {
      return static_cast<std::uint8_t>(a.venue) < static_cast<std::uint8_t>(b.venue);
    }
    return a.sequence < b.sequence;
  });

  const std::uint64_t t_min = all.front().exchange_ts_ns;
  const std::uint64_t t_max = all.back().exchange_ts_ns;
  assert(t_min == expected.t_min);
  assert(t_max == expected.t_max);

  const double holdout_frac = 0.30;
  const std::uint64_t holdout_start =
      t_min + static_cast<std::uint64_t>(static_cast<double>(t_max - t_min) * (1.0 - holdout_frac));
  assert(holdout_start == expected.holdout_start_ns);

  std::vector<mf::core::BookEvent> holdout;
  holdout.reserve(all.size() / 3 + 1);
  for (const auto& ev : all) {
    if (ev.exchange_ts_ns >= holdout_start) {
      holdout.push_back(ev);
    }
  }
  assert(holdout.size() == expected.holdout_events);
  assert(all.size() == expected.total_events);

  mf::research::ExperimentConfig cfg{};
  cfg.strategy_kind = mf::research::ExperimentConfig::StrategyKind::Ofi;
  cfg.ofi_strategy.threshold = 100.0;
  cfg.ofi_strategy.max_position = 200;
  cfg.ofi_strategy.quote_size = 20;
  cfg.pnl.maker_rebate_per_share = 0.0020;
  cfg.pnl.taker_fee_per_share = 0.0030;

  mf::research::ExperimentRunner runner(cfg);
  mf::research::ExperimentReport report{};
  assert(runner.run_on_events(holdout, report));

  std::vector<double> equity_deltas;
  for (std::size_t i = 1; i < report.equity_curve.size(); ++i) {
    const double d = report.equity_curve[i] - report.equity_curve[i - 1];
    if (d != 0.0) {
      equity_deltas.push_back(d);
    }
  }
  const auto boot = mf::research::block_bootstrap_mean_pvalue(equity_deltas, 92, 2000, 42);

  assert(report.fills == expected.fills);
  assert(report.submitted_orders == expected.submitted_orders);
  assert(near(report.pnl.sharpe, expected.sharpe_ann, kSharpeTol));
  assert(near(boot.p_value_greater_than_zero, expected.bootstrap_p_value, kMetricTol));

  const std::string baseline_md = read_text("bench/results/alpha_lab/baseline_ofi_backtest.md");
  assert(!baseline_md.empty());
  assert(baseline_md.find("0.10279") != std::string::npos);
  assert(baseline_md.find("401") != std::string::npos);

  const std::string manifest = read_text("bench/results/alpha_lab/baseline_manifest.json");
  assert(manifest.find("\"journal_crc\": 2369313900") != std::string::npos ||
         manifest.find("\"journal_crc\":2369313900") != std::string::npos);

  std::printf(
      "PASS alpha_lab_baseline sharpe=%.5f fills=%llu bootstrap_p=%.4f\n",
      report.pnl.sharpe,
      static_cast<unsigned long long>(report.fills),
      boot.p_value_greater_than_zero);
  return 0;
#endif
}
