#pragma once

#include <string>

namespace mf::bench {

struct RunMetadata {
  std::string git_sha{};
  std::string build_type{};
  std::string cxx_flags{};
  std::string host{};
  std::string cpu_model{};
  int cpu_count{0};
  std::string kernel{};
  std::string utc_timestamp{};
  std::string command_line{};
};

RunMetadata capture_run_metadata(int argc = 0, char** argv = nullptr);
std::string run_metadata_to_json(const RunMetadata& m);

}  // namespace mf::bench
