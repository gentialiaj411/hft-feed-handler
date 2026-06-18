#include <cassert>
#include <cstdint>
#include <vector>

#include "mf/research/alpha_lab/purged_cv.hpp"
#include "mf/research/alpha_lab/types.hpp"

namespace {

mf::research::alpha_lab::LabeledFeatureRow row(std::uint64_t ts, double mid) {
  mf::research::alpha_lab::LabeledFeatureRow r{};
  r.symbol_u64 = 1;
  r.exchange_ts_ns = ts;
  r.mid = mid;
  r.forward_mid_returns = {0.01, 0.02, 0.03};
  return r;
}

}  // namespace

int main() {
  std::vector<mf::research::alpha_lab::LabeledFeatureRow> rows;
  for (std::uint64_t i = 0; i < 100; ++i) {
    rows.push_back(row(i * 1'000'000'000ULL, 100.0 + static_cast<double>(i)));
  }

  mf::research::alpha_lab::PurgedCv cv{mf::research::alpha_lab::PurgedCvConfig{
      .n_folds = 5,
      .purge_ns = 2'000'000'000ULL,
      .embargo_ns = 1'000'000'000ULL,
  }};
  const auto folds = cv.split(rows);
  assert(folds.size() == 5);
  for (const auto& fold : folds) {
    assert(!fold.test_indices.empty());
    assert(!fold.train_indices.empty());
    assert(!cv.is_leaky_split(fold, 1'000'000'000ULL, rows));
  }

  auto leaky_cfg = mf::research::alpha_lab::PurgedCvConfig{
      .n_folds = 2,
      .purge_ns = 0,
      .embargo_ns = 0,
  };
  mf::research::alpha_lab::PurgedCv leaky_cv(leaky_cfg);
  const auto leaky_folds = leaky_cv.split(rows);
  assert(!leaky_folds.empty());
  bool saw_train_label_overlap = false;
  for (const auto& fold : leaky_folds) {
    for (std::size_t train_idx : fold.train_indices) {
      const std::uint64_t label_end = rows[train_idx].exchange_ts_ns + 1'000'000'000ULL;
      if (label_end >= fold.test_start_ns && rows[train_idx].exchange_ts_ns <= fold.test_end_ns) {
        saw_train_label_overlap = true;
      }
    }
  }
  assert(saw_train_label_overlap);
  return 0;
}
