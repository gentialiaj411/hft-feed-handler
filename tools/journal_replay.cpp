#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "mf/journal/journal_reader.hpp"
#include "mf/phase2/pipeline.hpp"
#include "mf/phase4/backtest_runner.hpp"

namespace {

std::string arg(int argc, char** argv, const std::string& key, const std::string& dflt) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] == key) return argv[i + 1];
  }
  return dflt;
}

std::string to_hex(std::uint32_t v) {
  std::ostringstream os;
  os << "0x" << std::hex << std::setw(8) << std::setfill('0') << v;
  return os.str();
}

struct CollectSink final : mf::phase2::IMergedEventSink {
  std::vector<mf::core::BookEvent> merged{};
  void on_merged_event(const mf::core::BookEvent& ev) noexcept override { merged.push_back(ev); }
};

std::string utc_stamp() {
  const auto now = std::chrono::system_clock::now();
  const auto tt = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  gmtime_r(&tt, &tm);
  char out[64];
  std::strftime(out, sizeof(out), "%Y%m%dT%H%M%SZ", &tm);
  return out;
}

}  // namespace

int main(int argc, char** argv) {
#if !defined(__linux__)
  std::printf("journal_replay is Linux-only\n");
  return 0;
#else
  const std::string path = arg(argc, argv, "--journal", "");
  const std::string mode = arg(argc, argv, "--mode", "both");
  if (path.empty()) {
    std::printf("usage: --journal <path> --mode {pipeline-crc|backtest|both}\n");
    return 2;
  }
  if (mode != "pipeline-crc" && mode != "backtest" && mode != "both") {
    std::printf("invalid --mode\n");
    return 2;
  }

  mf::journal::JournalReader reader;
  if (!reader.open(path)) {
    std::printf("failed to open journal: %s\n", path.c_str());
    return 1;
  }

  mf::phase2::Pipeline pipeline(1024, 1U << 20U);
  CollectSink sink{};
  mf::core::BookEvent ev{};
  std::uint64_t ts = 0;
  std::uint64_t seq = 0;
  while (reader.next(ev, ts, seq)) {
    ev.ingest_ts_ns = ts;
    pipeline.on_event(ev);
  }
  pipeline.finalize(&sink);

  const auto& jr = reader.stats();
  const auto& ps = pipeline.stats();
  std::printf("records_read=%llu crc_failures=%llu merged_crc=%s\n",
              static_cast<unsigned long long>(jr.records_read),
              static_cast<unsigned long long>(jr.crc_failures),
              to_hex(ps.merged_crc).c_str());

  mf::phase4::BacktestReport rep{};
  if (mode == "backtest" || mode == "both") {
    mf::phase4::BacktestRunner runner;
    rep = runner.run(sink.merged);
    std::printf("backtest pnl=%.6f sharpe=%.6f fills=%llu max_dd=%.6f\n",
                rep.realized_pnl,
                rep.sharpe,
                static_cast<unsigned long long>(rep.fills),
                rep.max_drawdown);
  }

  if (mode == "both") {
    std::filesystem::create_directories("bench/results");
    const std::string json_path = "bench/results/journal_replay_" + utc_stamp() + ".json";
    std::ofstream os(json_path);
    os << "{"
       << "\"journal_path\":\"" << path << "\","
       << "\"records_read\":" << jr.records_read << ","
       << "\"crc_failures\":" << jr.crc_failures << ","
       << "\"pipeline_crc_hex\":\"" << to_hex(ps.merged_crc) << "\","
       << "\"backtest_realized_pnl\":" << rep.realized_pnl << ","
       << "\"backtest_sharpe\":" << rep.sharpe << ","
       << "\"backtest_fill_count\":" << rep.fills << ","
       << "\"backtest_max_dd\":" << rep.max_drawdown
       << "}\n";
    std::printf("json=%s\n", json_path.c_str());
  }
  return 0;
#endif
}
