#include "mf/research/alpha_lab/signal_registry.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace mf::research::alpha_lab {

namespace {

double spread_from_row(const LabeledFeatureRow& row) {
  if (row.nbbo_bid_price == 0 || row.nbbo_ask_price == 0) {
    return 0.0;
  }
  return static_cast<double>(row.nbbo_ask_price - row.nbbo_bid_price);
}

double rolling_zscore(std::vector<double>& history, double value, std::size_t window) {
  history.push_back(value);
  if (history.size() > window) {
    history.erase(history.begin(), history.begin() + (history.size() - window));
  }
  if (history.size() < 2) {
    return 0.0;
  }
  const double mean = std::accumulate(history.begin(), history.end(), 0.0) /
                      static_cast<double>(history.size());
  double var = 0.0;
  for (double v : history) {
    const double d = v - mean;
    var += d * d;
  }
  var /= static_cast<double>(history.size());
  if (var <= 0.0) {
    return 0.0;
  }
  return (value - mean) / std::sqrt(var);
}

}  // namespace

std::vector<SignalDefinition> SignalRegistry::default_zoo() {
  std::vector<SignalDefinition> zoo;

  auto add = [&](const std::string& name, SignalFn fn) {
    zoo.push_back(SignalDefinition{.name = name, .compute = std::move(fn)});
  };

  add("ofi", [](const LabeledFeatureRow& row, SignalContext&) { return row.ofi; });
  add("microprice_minus_mid", [](const LabeledFeatureRow& row, SignalContext&) {
    return row.microprice - row.mid;
  });
  add("queue_ahead", [](const LabeledFeatureRow& row, SignalContext&) { return row.queue_ahead; });
  add("effective_spread", [](const LabeledFeatureRow& row, SignalContext&) {
    return row.effective_spread;
  });
  add("kyle_lambda", [](const LabeledFeatureRow& row, SignalContext&) { return row.kyle_lambda; });
  add("vpin", [](const LabeledFeatureRow& row, SignalContext&) { return row.vpin; });
  add("ofi_zscore", [](const LabeledFeatureRow& row, SignalContext& ctx) {
    return rolling_zscore(ctx.ofi_history, row.ofi, 100);
  });
  add("microprice_spread_adj", [](const LabeledFeatureRow& row, SignalContext&) {
    const double spread = spread_from_row(row);
    if (spread <= 0.0) {
      return 0.0;
    }
    return (row.microprice - row.mid) / spread;
  });
  add("ofi_times_spread", [](const LabeledFeatureRow& row, SignalContext&) {
    return row.ofi * spread_from_row(row);
  });
  add("sign_microprice_minus_mid", [](const LabeledFeatureRow& row, SignalContext&) {
    const double d = row.microprice - row.mid;
    if (d > 0.0) {
      return 1.0;
    }
    if (d < 0.0) {
      return -1.0;
    }
    return 0.0;
  });
  add("neg_effective_spread", [](const LabeledFeatureRow& row, SignalContext&) {
    return -row.effective_spread;
  });
  add("queue_over_spread", [](const LabeledFeatureRow& row, SignalContext&) {
    const double spread = std::max(1.0, spread_from_row(row));
    return row.queue_ahead / spread;
  });
  add("bid_ask_imbalance", [](const LabeledFeatureRow& row, SignalContext&) {
    const double denom = static_cast<double>(row.nbbo_bid_qty + row.nbbo_ask_qty);
    if (denom <= 0.0) {
      return 0.0;
    }
    return (static_cast<double>(row.nbbo_bid_qty) - static_cast<double>(row.nbbo_ask_qty)) / denom;
  });

  return zoo;
}

std::vector<double> SignalRegistry::extract(
    const SignalDefinition& signal,
    const std::vector<LabeledFeatureRow>& rows) const {
  std::vector<double> out;
  out.reserve(rows.size());
  SignalContext ctx{};
  ctx.rows = &rows;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    ctx.row_index = i;
    out.push_back(signal.compute(rows[i], ctx));
  }
  return out;
}

}  // namespace mf::research::alpha_lab
