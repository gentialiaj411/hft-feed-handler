#include "mf/research/alpha_lab/strategy_bridge.hpp"

#include "mf/research/alpha_lab/capacity_sweep.hpp"
#include "mf/research/alpha_lab/deflated_sharpe.hpp"
#include "mf/research/alpha_lab/signal_evaluator.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace mf::research::alpha_lab {

namespace {

std::vector<double> subset(
    const std::vector<double>& values,
    const std::vector<std::size_t>& indices) {
  std::vector<double> out;
  out.reserve(indices.size());
  for (std::size_t idx : indices) {
    if (idx < values.size()) {
      out.push_back(values[idx]);
    }
  }
  return out;
}

std::vector<double> fold_labels(
    const std::vector<LabeledFeatureRow>& rows,
    const std::vector<std::size_t>& indices,
    std::size_t horizon_index) {
  std::vector<double> labels;
  labels.reserve(indices.size());
  for (std::size_t idx : indices) {
    if (idx < rows.size() && horizon_index < rows[idx].forward_mid_returns.size()) {
      labels.push_back(rows[idx].forward_mid_returns[horizon_index]);
    } else {
      labels.push_back(kMissingLabel);
    }
  }
  return labels;
}

double proxy_sharpe(
    const std::vector<double>& signal,
    const std::vector<double>& labels) {
  std::vector<double> returns;
  returns.reserve(signal.size());
  for (std::size_t i = 0; i < signal.size() && i < labels.size(); ++i) {
    if (std::isnan(signal[i]) || std::isnan(labels[i])) {
      continue;
    }
    const double pos = signal[i] > 0.0 ? 1.0 : (signal[i] < 0.0 ? -1.0 : 0.0);
    returns.push_back(pos * labels[i]);
  }
  return sharpe_ratio(returns);
}

}  // namespace

std::vector<StrategyBridgeResult> StrategyBridge::evaluate_top_signal(
    const std::string& signal_name,
    const std::vector<LabeledFeatureRow>& rows,
    const std::vector<CvFold>& folds,
    const std::vector<double>& signal_values) const {
  std::vector<StrategyBridgeResult> out;
  for (const auto& fold : folds) {
    const auto test_signal = subset(signal_values, fold.test_indices);
    const auto test_labels = fold_labels(rows, fold.test_indices, 0);
    StrategyBridgeResult r{};
    r.signal = signal_name;
    r.fold_id = fold.fold_id;
    r.sharpe = proxy_sharpe(test_signal, test_labels);
    r.fills = test_signal.size() / 100;
    out.push_back(r);
  }
  return out;
}

std::vector<AblationRow> StrategyBridge::run_ablation(
    const SignalAggregate& top_signal,
    const std::vector<LabeledFeatureRow>& rows,
    const std::vector<CvFold>& folds,
    const std::vector<double>& signal_values,
    const double baseline_sharpe) const {
  std::vector<AblationRow> out;

  auto add_variant = [&](const std::string& name, const std::vector<double>& signal) {
    double ic_sum = 0.0;
    double qs_sum = 0.0;
    double sharpe_sum = 0.0;
    std::size_t n = 0;
    for (const auto& fold : folds) {
      const auto test_signal = subset(signal, fold.test_indices);
      const auto test_labels = fold_labels(rows, fold.test_indices, 0);
      ic_sum += SignalEvaluator::pearson_ic(test_signal, test_labels);
      qs_sum += SignalEvaluator::quintile_spread(test_signal, test_labels);
      sharpe_sum += proxy_sharpe(test_signal, test_labels);
      ++n;
    }
    if (n == 0) {
      return;
    }
    AblationRow row{};
    row.variant = name;
    row.mean_ic = ic_sum / static_cast<double>(n);
    row.mean_quintile_spread = qs_sum / static_cast<double>(n);
    row.net_sharpe = sharpe_sum / static_cast<double>(n);
    out.push_back(row);
  };

  add_variant("full_signal", signal_values);

  std::vector<double> zero_ofi = signal_values;
  for (std::size_t i = 0; i < rows.size() && i < zero_ofi.size(); ++i) {
    if (top_signal.signal.find("ofi") != std::string::npos) {
      zero_ofi[i] = 0.0;
    }
  }
  add_variant("ablate_ofi_component", zero_ofi);

  std::vector<double> zero_micro = signal_values;
  for (std::size_t i = 0; i < rows.size() && i < zero_micro.size(); ++i) {
    if (top_signal.signal.find("microprice") != std::string::npos) {
      zero_micro[i] = 0.0;
    }
  }
  add_variant("ablate_microprice_component", zero_micro);

  CapacitySweep sweep{};
  std::vector<double> spreads;
  spreads.reserve(rows.size());
  for (const auto& row : rows) {
    spreads.push_back(static_cast<double>(row.nbbo_ask_price - row.nbbo_bid_price));
  }
  std::vector<std::size_t> all_idx(rows.size());
  std::iota(all_idx.begin(), all_idx.end(), 0);
  const auto labels = fold_labels(rows, all_idx, 0);
  const auto curve = sweep.sweep(signal_values, labels, spreads);
  AblationRow no_cost{};
  no_cost.variant = "no_cost_model";
  no_cost.net_sharpe = curve.empty() ? 0.0 : curve.front().gross_sharpe;
  no_cost.mean_ic = top_signal.mean_ic;
  no_cost.mean_quintile_spread = top_signal.mean_quintile_spread;
  out.push_back(no_cost);

  AblationRow baseline{};
  baseline.variant = "phase0_ofi_baseline";
  baseline.net_sharpe = baseline_sharpe;
  baseline.mean_ic = top_signal.mean_ic;
  baseline.mean_quintile_spread = top_signal.mean_quintile_spread;
  out.push_back(baseline);

  return out;
}

}  // namespace mf::research::alpha_lab
