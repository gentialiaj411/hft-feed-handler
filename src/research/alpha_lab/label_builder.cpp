#include "mf/research/alpha_lab/label_builder.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mf::research::alpha_lab {

namespace {

[[nodiscard]] std::size_t find_future_mid_index(
    const std::vector<MidObservation>& timeline,
    std::size_t origin_index,
    std::uint64_t target_ts_ns) noexcept {
  const std::uint64_t symbol = timeline[origin_index].symbol_u64;
  for (std::size_t i = origin_index + 1; i < timeline.size(); ++i) {
    if (timeline[i].symbol_u64 != symbol) {
      continue;
    }
    if (timeline[i].exchange_ts_ns >= target_ts_ns) {
      return i;
    }
  }
  return timeline.size();
}

}  // namespace

LabelBuilder::LabelBuilder() = default;

LabelBuilder::LabelBuilder(Config cfg) : cfg_(cfg) {}

std::vector<MidObservation> LabelBuilder::build_timeline(const std::vector<LabeledFeatureRow>& rows) {
  std::vector<MidObservation> timeline;
  timeline.reserve(rows.size());
  for (const auto& row : rows) {
    if (row.mid <= 0.0) {
      continue;
    }
    timeline.push_back(MidObservation{
        .symbol_u64 = row.symbol_u64,
        .exchange_ts_ns = row.exchange_ts_ns,
        .mid = row.mid,
    });
  }
  std::stable_sort(timeline.begin(), timeline.end(), [](const MidObservation& a, const MidObservation& b) {
    if (a.symbol_u64 != b.symbol_u64) {
      return a.symbol_u64 < b.symbol_u64;
    }
    return a.exchange_ts_ns < b.exchange_ts_ns;
  });
  return timeline;
}

double LabelBuilder::forward_mid_return(
    const std::vector<MidObservation>& timeline,
    std::size_t origin_index,
    std::uint64_t horizon_ns) noexcept {
  if (origin_index >= timeline.size()) {
    return kMissingLabel;
  }
  const double mid_now = timeline[origin_index].mid;
  if (mid_now <= 0.0) {
    return kMissingLabel;
  }
  const std::uint64_t target_ts = timeline[origin_index].exchange_ts_ns + horizon_ns;
  const std::size_t future_index = find_future_mid_index(timeline, origin_index, target_ts);
  if (future_index >= timeline.size()) {
    return kMissingLabel;
  }
  const double mid_future = timeline[future_index].mid;
  if (mid_future <= 0.0) {
    return kMissingLabel;
  }
  return (mid_future - mid_now) / mid_now;
}

void LabelBuilder::attach_labels(std::vector<LabeledFeatureRow>& rows) const {
  const auto timeline = build_timeline(rows);
  const std::size_t label_count = cfg_.horizons.horizons_ns.size();
  for (auto& row : rows) {
    row.forward_mid_returns.assign(label_count, kMissingLabel);
    if (row.mid <= 0.0) {
      continue;
    }
    const auto it = std::lower_bound(
        timeline.begin(),
        timeline.end(),
        MidObservation{.symbol_u64 = row.symbol_u64, .exchange_ts_ns = row.exchange_ts_ns, .mid = row.mid},
        [](const MidObservation& a, const MidObservation& b) {
          if (a.symbol_u64 != b.symbol_u64) {
            return a.symbol_u64 < b.symbol_u64;
          }
          return a.exchange_ts_ns < b.exchange_ts_ns;
        });
    if (it == timeline.end() || it->symbol_u64 != row.symbol_u64 || it->exchange_ts_ns != row.exchange_ts_ns) {
      continue;
    }
    const std::size_t origin_index = static_cast<std::size_t>(it - timeline.begin());
    for (std::size_t h = 0; h < label_count; ++h) {
      row.forward_mid_returns[h] =
          forward_mid_return(timeline, origin_index, cfg_.horizons.horizons_ns[h]);
    }
  }
}

}  // namespace mf::research::alpha_lab
