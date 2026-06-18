#pragma once

#include <cstddef>
#include <vector>

#include "mf/research/alpha_lab/types.hpp"

namespace mf::research::alpha_lab {

class LabelBuilder {
 public:
  struct Config {
    LabelHorizons horizons{};
  };

  LabelBuilder();
  explicit LabelBuilder(Config cfg);

  void attach_labels(std::vector<LabeledFeatureRow>& rows) const;

  [[nodiscard]] static double forward_mid_return(
      const std::vector<MidObservation>& timeline,
      std::size_t origin_index,
      std::uint64_t horizon_ns) noexcept;

  [[nodiscard]] static std::vector<MidObservation> build_timeline(
      const std::vector<LabeledFeatureRow>& rows);

 private:
  Config cfg_{};
};

}  // namespace mf::research::alpha_lab
