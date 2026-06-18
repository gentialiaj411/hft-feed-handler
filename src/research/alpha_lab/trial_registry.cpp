#include "mf/research/alpha_lab/trial_registry.hpp"

#include <fstream>
#include <iomanip>

namespace mf::research::alpha_lab {

TrialRegistry::TrialRegistry(std::string path) : path_(std::move(path)) {}

bool TrialRegistry::append(const TrialRecord& record) {
  std::ofstream out(path_, std::ios::app);
  if (!out) {
    return false;
  }
  out << "{\"signal\":\"" << record.signal << "\","
      << "\"horizon_ns\":" << record.horizon_ns << ","
      << "\"fold_id\":" << record.fold_id << ","
      << "\"ic\":" << std::setprecision(17) << record.ic << ","
      << "\"hit_rate\":" << record.hit_rate << ","
      << "\"quintile_spread\":" << record.quintile_spread << ","
      << "\"n_obs\":" << record.n_obs << "}\n";
  ++count_;
  return true;
}

}  // namespace mf::research::alpha_lab
