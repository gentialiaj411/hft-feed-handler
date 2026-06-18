#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "mf/research/alpha_lab/types.hpp"

namespace mf::research::alpha_lab {

struct SignalContext {
  std::size_t row_index{0};
  const std::vector<LabeledFeatureRow>* rows{nullptr};
  std::vector<double> ofi_history{};
};

using SignalFn = std::function<double(const LabeledFeatureRow&, SignalContext&)>;

struct SignalDefinition {
  std::string name{};
  SignalFn compute{};
};

class SignalRegistry {
 public:
  [[nodiscard]] static std::vector<SignalDefinition> default_zoo();

  [[nodiscard]] std::vector<double> extract(
      const SignalDefinition& signal,
      const std::vector<LabeledFeatureRow>& rows) const;
};

}  // namespace mf::research::alpha_lab
