#pragma once

#include <string>
#include <vector>

#include "mf/phase3/feature_pipeline.hpp"
#include "mf/research/alpha_lab/label_builder.hpp"
#include "mf/research/alpha_lab/types.hpp"

namespace mf::research::alpha_lab {

class FeatureStore {
 public:
  struct Config {
    LabelBuilder::Config labels{};
    mf::phase3::FeaturePipeline::Config pipeline{};
  };

  FeatureStore();
  explicit FeatureStore(Config cfg);

  bool materialize_from_journal(
      const std::string& journal_path,
      std::vector<LabeledFeatureRow>& out,
      MaterializeStats& stats) const;

  [[nodiscard]] bool write_csv(
      const std::string& path,
      const std::vector<LabeledFeatureRow>& rows) const;

 private:
  Config cfg_{};
};

}  // namespace mf::research::alpha_lab
