#include "mf/research/alpha_lab/signal_evaluator.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <vector>

namespace mf::research::alpha_lab {

namespace {

struct Pair {
  double signal{0.0};
  double label{0.0};
};

std::vector<Pair> valid_pairs(const std::vector<double>& signal, const std::vector<double>& labels) {
  std::vector<Pair> pairs;
  const std::size_t n = std::min(signal.size(), labels.size());
  pairs.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    if (std::isnan(signal[i]) || std::isnan(labels[i])) {
      continue;
    }
    pairs.push_back(Pair{.signal = signal[i], .label = labels[i]});
  }
  return pairs;
}

}  // namespace

double SignalEvaluator::pearson_ic(
    const std::vector<double>& signal,
    const std::vector<double>& labels) {
  const auto pairs = valid_pairs(signal, labels);
  if (pairs.size() < 3) {
    return 0.0;
  }
  double sx = 0.0;
  double sy = 0.0;
  for (const auto& p : pairs) {
    sx += p.signal;
    sy += p.label;
  }
  const double n = static_cast<double>(pairs.size());
  const double mx = sx / n;
  const double my = sy / n;
  double cov = 0.0;
  double vx = 0.0;
  double vy = 0.0;
  for (const auto& p : pairs) {
    const double dx = p.signal - mx;
    const double dy = p.label - my;
    cov += dx * dy;
    vx += dx * dx;
    vy += dy * dy;
  }
  if (vx <= 0.0 || vy <= 0.0) {
    return 0.0;
  }
  return cov / std::sqrt(vx * vy);
}

double SignalEvaluator::hit_rate(
    const std::vector<double>& signal,
    const std::vector<double>& labels) {
  const auto pairs = valid_pairs(signal, labels);
  if (pairs.empty()) {
    return 0.0;
  }
  std::size_t hits = 0;
  for (const auto& p : pairs) {
    if ((p.signal > 0.0 && p.label > 0.0) || (p.signal < 0.0 && p.label < 0.0)) {
      ++hits;
    }
  }
  return static_cast<double>(hits) / static_cast<double>(pairs.size());
}

double SignalEvaluator::quintile_spread(
    const std::vector<double>& signal,
    const std::vector<double>& labels) {
  auto pairs = valid_pairs(signal, labels);
  if (pairs.size() < 10) {
    return 0.0;
  }
  std::sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) {
    return a.signal < b.signal;
  });
  const std::size_t q = std::max<std::size_t>(1, pairs.size() / 5);
  double top = 0.0;
  double bottom = 0.0;
  for (std::size_t i = 0; i < q; ++i) {
    bottom += pairs[i].label;
    top += pairs[pairs.size() - 1 - i].label;
  }
  return (top - bottom) / static_cast<double>(q);
}

SignalFoldMetrics SignalEvaluator::evaluate_fold(
    const std::string& signal_name,
    const std::uint64_t horizon_ns,
    const std::vector<double>& signal,
    const std::vector<double>& labels,
    const std::size_t fold_id) {
  SignalFoldMetrics m{};
  m.signal = signal_name;
  m.horizon_ns = horizon_ns;
  m.fold_id = fold_id;
  m.ic = pearson_ic(signal, labels);
  m.hit_rate = hit_rate(signal, labels);
  m.quintile_spread = quintile_spread(signal, labels);
  m.n_obs = valid_pairs(signal, labels).size();
  return m;
}

std::vector<SignalAggregate> SignalEvaluator::aggregate(
    const std::vector<SignalFoldMetrics>& per_fold) {
  struct Key {
    std::string signal;
    std::uint64_t horizon_ns{0};
    bool operator<(const Key& o) const {
      if (signal != o.signal) {
        return signal < o.signal;
      }
      return horizon_ns < o.horizon_ns;
    }
  };

  std::map<Key, std::vector<const SignalFoldMetrics*>> grouped;
  for (const auto& m : per_fold) {
    grouped[Key{.signal = m.signal, .horizon_ns = m.horizon_ns}].push_back(&m);
  }

  std::vector<SignalAggregate> out;
  for (const auto& [key, items] : grouped) {
    SignalAggregate agg{};
    agg.signal = key.signal;
    agg.horizon_ns = key.horizon_ns;
    agg.n_folds = items.size();
    double ic_sum = 0.0;
    double qs_sum = 0.0;
    double ic_sq = 0.0;
    double qs_sq = 0.0;
    for (const auto* item : items) {
      ic_sum += item->ic;
      qs_sum += item->quintile_spread;
      ic_sq += item->ic * item->ic;
      qs_sq += item->quintile_spread * item->quintile_spread;
    }
    const double n = static_cast<double>(items.size());
    agg.mean_ic = ic_sum / n;
    agg.mean_quintile_spread = qs_sum / n;
    const double ic_var = std::max(0.0, ic_sq / n - agg.mean_ic * agg.mean_ic);
    const double qs_var = std::max(0.0, qs_sq / n - agg.mean_quintile_spread * agg.mean_quintile_spread);
    agg.std_ic = std::sqrt(ic_var);
    agg.std_quintile_spread = std::sqrt(qs_var);
    out.push_back(agg);
  }
  std::sort(out.begin(), out.end(), [](const SignalAggregate& a, const SignalAggregate& b) {
    return a.mean_ic > b.mean_ic;
  });
  return out;
}

}  // namespace mf::research::alpha_lab
