#include "mf/research/alpha_lab/purged_cv.hpp"

#include <algorithm>
#include <numeric>

namespace mf::research::alpha_lab {

PurgedCv::PurgedCv() = default;

PurgedCv::PurgedCv(PurgedCvConfig cfg) : cfg_(cfg) {}

std::vector<CvFold> PurgedCv::split(const std::vector<LabeledFeatureRow>& rows) const {
  std::vector<CvFold> folds;
  if (rows.empty() || cfg_.n_folds < 2) {
    return folds;
  }

  std::vector<std::size_t> order(rows.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](const std::size_t a, const std::size_t b) {
    if (rows[a].exchange_ts_ns != rows[b].exchange_ts_ns) {
      return rows[a].exchange_ts_ns < rows[b].exchange_ts_ns;
    }
    return a < b;
  });

  const std::size_t fold_size = std::max<std::size_t>(1, order.size() / cfg_.n_folds);
  for (std::size_t f = 0; f < cfg_.n_folds; ++f) {
    CvFold fold{};
    fold.fold_id = f;
    const std::size_t start = f * fold_size;
    const std::size_t end =
        (f + 1 == cfg_.n_folds) ? order.size() : std::min(order.size(), (f + 1) * fold_size);
    if (start >= end) {
      continue;
    }
    fold.test_start_ns = rows[order[start]].exchange_ts_ns;
    fold.test_end_ns = rows[order[end - 1]].exchange_ts_ns;
    for (std::size_t i = start; i < end; ++i) {
      fold.test_indices.push_back(order[i]);
    }

    const std::uint64_t purge_start =
        (fold.test_start_ns > cfg_.purge_ns) ? fold.test_start_ns - cfg_.purge_ns : 0;
    const std::uint64_t embargo_end = fold.test_end_ns + cfg_.embargo_ns;

    for (std::size_t i = 0; i < order.size(); ++i) {
      const std::size_t idx = order[i];
      const std::uint64_t ts = rows[idx].exchange_ts_ns;
      const bool in_test = (i >= start && i < end);
      const bool in_purge_embargo = (ts >= purge_start && ts <= embargo_end);
      if (!in_test && !in_purge_embargo) {
        fold.train_indices.push_back(idx);
      }
    }
    folds.push_back(std::move(fold));
  }
  return folds;
}

bool PurgedCv::is_leaky_split(
    const CvFold& fold,
    const std::uint64_t label_horizon_ns,
    const std::vector<LabeledFeatureRow>& rows) const {
  for (const std::size_t train_idx : fold.train_indices) {
    const std::uint64_t ts = rows[train_idx].exchange_ts_ns;
    const std::uint64_t label_end = ts + label_horizon_ns;
    if (label_end >= fold.test_start_ns && ts <= fold.test_end_ns) {
      return true;
    }
  }
  return false;
}

}  // namespace mf::research::alpha_lab
