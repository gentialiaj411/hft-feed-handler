#pragma once

#include <cstdint>
#include <string>

namespace mf::research::alpha_lab {

struct TrialRecord {
  std::string signal{};
  std::uint64_t horizon_ns{0};
  std::size_t fold_id{0};
  double ic{0.0};
  double hit_rate{0.0};
  double quintile_spread{0.0};
  std::size_t n_obs{0};
};

class TrialRegistry {
 public:
  explicit TrialRegistry(std::string path);

  bool append(const TrialRecord& record);
  [[nodiscard]] std::size_t count() const { return count_; }

 private:
  std::string path_;
  std::size_t count_{0};
};

}  // namespace mf::research::alpha_lab
