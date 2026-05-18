#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "mf/bench/run_metadata.hpp"
#include "mf/research/event_store.hpp"
#include "mf/research/experiment_runner.hpp"

namespace {
std::string arg(int argc, char** argv, const std::string& key, const std::string& dflt) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] == key) return argv[i + 1];
  }
  return dflt;
}

std::string utc_stamp() {
  const auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &tt);
#else
  gmtime_r(&tt, &tm);
#endif
  char out[64];
  std::strftime(out, sizeof(out), "%Y%m%dT%H%M%SZ", &tm);
  return out;
}
}  // namespace

int main(int argc, char** argv) {
#if !defined(__linux__)
  std::printf("experiment_runner is Linux-only because EventStore uses mmap journal backend\n");
  return 0;
#else
  const std::string journal = arg(argc, argv, "--journal", "");
  const std::string out = arg(argc, argv, "--out", "");
  if (journal.empty()) {
    std::printf("usage: experiment_runner --journal <path> [--out <json>]\n");
    return 2;
  }

  mf::research::EventStore store(journal);
  mf::research::ExperimentRunner runner;
  mf::research::ExperimentReport report{};
  if (!runner.run(store, report)) {
    std::printf("experiment failed: crc_failures=%llu clock_rejects=%llu\n",
                static_cast<unsigned long long>(report.input.crc_failures),
                static_cast<unsigned long long>(report.clock_rejects));
    return 1;
  }

  const std::string payload = std::string("{\"metadata\":") +
                              mf::bench::run_metadata_to_json(mf::bench::capture_run_metadata(argc, argv)) +
                              ",\"experiment\":" +
                              mf::research::experiment_report_to_json(report, journal) +
                              "}\n";

  std::string out_path = out;
  if (out_path.empty()) {
    std::filesystem::create_directories("bench/results");
    out_path = "bench/results/experiment_" + utc_stamp() + ".json";
  }
  std::ofstream os(out_path);
  os << payload;
  std::printf("json=%s records=%llu orders=%llu fills=%llu output_hash=%u\n",
              out_path.c_str(),
              static_cast<unsigned long long>(report.input.records),
              static_cast<unsigned long long>(report.submitted_orders),
              static_cast<unsigned long long>(report.fills),
              report.output_hash);
  return 0;
#endif
}
