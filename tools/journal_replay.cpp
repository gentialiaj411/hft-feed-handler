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

std::uint64_t arg_u64(int argc, char** argv, const std::string& key, std::uint64_t dflt) {
  const std::string v = arg(argc, argv, key, "");
  if (v.empty()) return dflt;
  return static_cast<std::uint64_t>(std::stoull(v));
}

bool has_flag(int argc, char** argv, const std::string& key) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == key) return true;
  }
  return false;
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
  const std::string seed = arg(argc, argv, "--seed", "");
  const std::uint64_t max_records = arg_u64(argc, argv, "--max-records", 0);
  const bool no_finalize = has_flag(argc, argv, "--no-finalize");
  const bool verbose = has_flag(argc, argv, "--verbose");
  (void)seed;
  if (path.empty()) {
    std::fprintf(stderr, "journal_replay: record_decode at offset=0 (detail=missing_journal_arg)\n");
    std::fprintf(stderr, "journal_replay: exiting nonzero, reason=record_decode\n");
    return 2;
  }
  if (mode != "pipeline-crc" && mode != "backtest" && mode != "both") {
    std::fprintf(stderr, "journal_replay: record_decode at offset=0 (detail=invalid_mode)\n");
    std::fprintf(stderr, "journal_replay: exiting nonzero, reason=record_decode\n");
    return 2;
  }

  mf::journal::JournalReader reader;
  if (!reader.open(path)) {
    const char* r = reader.error_reason() ? reader.error_reason() : "record_decode";
    std::fprintf(stderr, "journal_replay: %s at offset=%zu (detail=open_failed:%s)\n", r, reader.error_offset(), path.c_str());
    std::fprintf(stderr, "journal_replay: exiting nonzero, reason=%s\n", r);
    return 1;
  }

  mf::phase2::Pipeline pipeline(1024, 1U << 20U);
  CollectSink sink{};
  mf::core::BookEvent ev{};
  std::uint64_t ts = 0;
  std::uint64_t seq = 0;
  while (reader.next(ev, ts, seq)) {
    ev.ingest_ts_ns = ts;
    if (verbose && (reader.stats().records_read <= 100 || (reader.stats().records_read % 100000ULL) == 0ULL)) {
      std::fprintf(stderr,
                   "journal_replay: before_pipeline read=%llu journal_seq=%llu event_seq=%llu event_type=%u raw_type=%u ts=%llu\n",
                   static_cast<unsigned long long>(reader.stats().records_read),
                   static_cast<unsigned long long>(seq),
                   static_cast<unsigned long long>(ev.sequence),
                   static_cast<unsigned>(ev.type),
                   static_cast<unsigned>(ev.raw_type),
                   static_cast<unsigned long long>(ev.exchange_ts_ns));
    }
    pipeline.on_event(ev);
    if (verbose && (reader.stats().records_read <= 100 || (reader.stats().records_read % 100000ULL) == 0ULL)) {
      std::fprintf(stderr,
                   "journal_replay: after_pipeline read=%llu journal_seq=%llu event_seq=%llu event_type=%u accepted=%llu buffered=%llu gaps=%llu\n",
                   static_cast<unsigned long long>(reader.stats().records_read),
                   static_cast<unsigned long long>(seq),
                   static_cast<unsigned long long>(ev.sequence),
                   static_cast<unsigned>(ev.type),
                   static_cast<unsigned long long>(pipeline.stats().accepted),
                   static_cast<unsigned long long>(pipeline.stats().buffered_out_of_order),
                   static_cast<unsigned long long>(pipeline.stats().recovery_requests));
    }
    if (max_records > 0 && reader.stats().records_read >= max_records) {
      break;
    }
  }
  if (reader.had_error()) {
    const char* r = reader.error_reason() ? reader.error_reason() : "record_decode";
    std::fprintf(stderr, "journal_replay: %s at offset=%zu (detail=reader_next_failed)\n", r, reader.error_offset());
    std::fprintf(stderr, "journal_replay: exiting nonzero, reason=%s\n", r);
    return 1;
  }
  if (no_finalize) {
    const auto& jr = reader.stats();
    const auto& ps = pipeline.stats();
    std::printf("records_read=%llu crc_failures=%llu accepted=%llu buffered=%llu dropped_gap_too_large=%llu recovery_requests=%llu\n",
                static_cast<unsigned long long>(jr.records_read),
                static_cast<unsigned long long>(jr.crc_failures),
                static_cast<unsigned long long>(ps.accepted),
                static_cast<unsigned long long>(ps.buffered_out_of_order),
                static_cast<unsigned long long>(ps.dropped_gap_too_large),
                static_cast<unsigned long long>(ps.recovery_requests));
    return 0;
  }

  if (verbose) {
    std::fprintf(stderr, "journal_replay: finalizing records=%llu\n",
                 static_cast<unsigned long long>(reader.stats().records_read));
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
    std::printf("backtest realized_pnl=%.6f unrealized_pnl=%.6f total_pnl=%.6f sharpe=%.6f fill_ratio=%.6f fills=%llu max_dd=%.6f\n",
                rep.realized_pnl,
                rep.unrealized_pnl,
                rep.total_pnl,
                rep.sharpe,
                rep.fill_ratio,
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
       << "\"backtest_unrealized_pnl\":" << rep.unrealized_pnl << ","
       << "\"backtest_total_pnl\":" << rep.total_pnl << ","
       << "\"backtest_sharpe\":" << rep.sharpe << ","
       << "\"backtest_fill_ratio\":" << rep.fill_ratio << ","
       << "\"backtest_fill_count\":" << rep.fills << ","
       << "\"backtest_max_dd\":" << rep.max_drawdown
       << "}\n";
    std::printf("json=%s\n", json_path.c_str());
  }
  return 0;
#endif
}
