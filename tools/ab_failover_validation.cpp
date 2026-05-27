#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/journal/journal_reader.hpp"
#include "mf/phase2/ab_arbiter.hpp"
#include "mf/phase2/pipeline.hpp"

namespace {

struct Config {
  std::string journal_path{};
  std::string output_path{};
  std::uint64_t seed{12345};
  double drop_rate_a{0.5};
  std::uint64_t gap_window{1024};
  std::size_t capacity{1U << 20U};
};

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

double arg_double(int argc, char** argv, const std::string& key, double dflt) {
  const std::string v = arg(argc, argv, key, "");
  if (v.empty()) return dflt;
  return std::stod(v);
}

std::string utc_date() {
  const auto now = std::chrono::system_clock::now();
  const auto tt = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  gmtime_r(&tt, &tm);
  char out[32];
  std::strftime(out, sizeof(out), "%Y-%m-%d", &tm);
  return out;
}

std::string run_cmd_line(const char* cmd) {
  FILE* p = popen(cmd, "r");
  if (p == nullptr) return {};
  char buf[256];
  std::string out;
  while (std::fgets(buf, static_cast<int>(sizeof(buf)), p) != nullptr) {
    out += buf;
  }
  const int rc = pclose(p);
  if (rc != 0) return {};
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
    out.pop_back();
  }
  return out;
}

std::string detect_commit() {
  const std::string sha = run_cmd_line("git rev-parse --short=12 HEAD");
  return sha.empty() ? "unknown" : sha;
}

std::string detect_host() {
  const char* host = std::getenv("HOSTNAME");
  if (host != nullptr && host[0] != '\0') return host;
  return "wsl2";
}

struct CollectSink final : mf::phase2::IMergedEventSink {
  std::vector<mf::core::BookEvent> merged{};
  void on_merged_event(const mf::core::BookEvent& ev) noexcept override { merged.push_back(ev); }
};

bool same_event(const mf::core::BookEvent& a, const mf::core::BookEvent& b) {
  return a.venue == b.venue &&
         a.type == b.type &&
         a.sequence == b.sequence &&
         a.exchange_ts_ns == b.exchange_ts_ns &&
         a.side == b.side &&
         a.price == b.price &&
         a.qty == b.qty &&
         a.order_id == b.order_id &&
         a.raw_type == b.raw_type &&
         a.symbol.bytes == b.symbol.bytes;
}

std::uint64_t divergence_count(const std::vector<mf::core::BookEvent>& a,
                               const std::vector<mf::core::BookEvent>& b) {
  const std::size_t min_sz = (a.size() < b.size()) ? a.size() : b.size();
  std::uint64_t div = 0;
  for (std::size_t i = 0; i < min_sz; ++i) {
    if (!same_event(a[i], b[i])) {
      ++div;
    }
  }
  if (a.size() > b.size()) {
    div += static_cast<std::uint64_t>(a.size() - b.size());
  } else if (b.size() > a.size()) {
    div += static_cast<std::uint64_t>(b.size() - a.size());
  }
  return div;
}

bool parse_args(int argc, char** argv, Config& cfg) {
  cfg.journal_path = arg(argc, argv, "--journal", "");
  cfg.output_path = arg(argc, argv, "--output", "");
  cfg.seed = arg_u64(argc, argv, "--seed", cfg.seed);
  cfg.drop_rate_a = arg_double(argc, argv, "--drop-rate-a", cfg.drop_rate_a);
  cfg.gap_window = arg_u64(argc, argv, "--gap-window", cfg.gap_window);
  cfg.capacity = static_cast<std::size_t>(arg_u64(argc, argv, "--capacity", cfg.capacity));
  if (cfg.journal_path.empty()) {
    std::fprintf(stderr, "ab_failover_validation: --journal is required\n");
    return false;
  }
  if (cfg.output_path.empty()) {
    const std::string commit = detect_commit();
    cfg.output_path = "bench/results/ab_failover_validation_wsl2_" + commit + ".json";
  }
  if (cfg.drop_rate_a < 0.0 || cfg.drop_rate_a > 1.0) {
    std::fprintf(stderr, "ab_failover_validation: --drop-rate-a must be in [0,1]\n");
    return false;
  }
  return true;
}

bool load_journal(const std::string& path, std::vector<mf::core::BookEvent>& out) {
  mf::journal::JournalReader reader;
  if (!reader.open(path)) {
    std::fprintf(stderr, "ab_failover_validation: failed to open journal: %s\n", path.c_str());
    return false;
  }
  mf::core::BookEvent ev{};
  std::uint64_t ingest_ts_ns = 0;
  std::uint64_t mono_seq = 0;
  while (reader.next(ev, ingest_ts_ns, mono_seq)) {
    ev.ingest_ts_ns = ingest_ts_ns;
    out.push_back(ev);
  }
  if (reader.had_error()) {
    std::fprintf(stderr, "ab_failover_validation: journal read error at offset=%zu\n", reader.error_offset());
    return false;
  }
  return true;
}

struct RunOutcome {
  std::uint32_t crc{0};
  std::vector<mf::core::BookEvent> merged{};
};

RunOutcome run_baseline(const std::vector<mf::core::BookEvent>& source, const Config& cfg) {
  mf::phase2::Pipeline pipeline(cfg.gap_window, cfg.capacity);
  CollectSink sink{};
  for (const auto& ev : source) {
    pipeline.on_event(ev);
  }
  pipeline.finalize(&sink);
  return RunOutcome{pipeline.stats().merged_crc, std::move(sink.merged)};
}

RunOutcome run_drop_injected(const std::vector<mf::core::BookEvent>& source,
                             const Config& cfg,
                             mf::phase2::DroppedFeedCounts* drops) {
  const auto raced = mf::phase2::make_dual_feed_race_stream(
      source,
      mf::phase2::DualFeedDropConfig{cfg.drop_rate_a, 0.0, cfg.seed},
      drops);
  mf::phase2::AbArbiter arb(cfg.gap_window);
  mf::phase2::Pipeline pipeline(cfg.gap_window, cfg.capacity);
  CollectSink sink{};
  for (const auto& pair : raced) {
    (void)arb.on_event(pair.first, pair.second);
    auto ready = arb.drain_ready();
    for (const auto& ev : ready) {
      pipeline.on_event(ev);
    }
  }
  pipeline.finalize(&sink);
  return RunOutcome{pipeline.stats().merged_crc, std::move(sink.merged)};
}

std::string crc_hex(std::uint32_t v) {
  std::ostringstream os;
  os << "0x" << std::hex << std::setw(8) << std::setfill('0') << v;
  return os.str();
}

}  // namespace

int main(int argc, char** argv) {
#if !defined(__linux__)
  std::fprintf(stderr, "ab_failover_validation is Linux-only\n");
  return 2;
#else
  Config cfg{};
  if (!parse_args(argc, argv, cfg)) {
    return 2;
  }

  std::vector<mf::core::BookEvent> source{};
  if (!load_journal(cfg.journal_path, source)) {
    return 1;
  }

  const RunOutcome baseline = run_baseline(source, cfg);
  mf::phase2::DroppedFeedCounts drops{};
  const RunOutcome injected = run_drop_injected(source, cfg, &drops);

  const bool crc_match = baseline.crc == injected.crc;
  const std::uint64_t div_count = divergence_count(baseline.merged, injected.merged);

  if (const auto parent = std::filesystem::path(cfg.output_path).parent_path();
      !parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream os(cfg.output_path);
  os << "{\n";
  os << "  \"run_metadata\": {\n";
  os << "    \"commit\": \"" << detect_commit() << "\",\n";
  os << "    \"host\": \"" << detect_host() << "\",\n";
  os << "    \"date\": \"" << utc_date() << "\",\n";
  os << "    \"seed\": " << cfg.seed << "\n";
  os << "  },\n";
  os << "  \"baseline_crc\": \"" << crc_hex(baseline.crc) << "\",\n";
  os << "  \"drop_injected_crc\": \"" << crc_hex(injected.crc) << "\",\n";
  os << "  \"events_processed\": " << source.size() << ",\n";
  os << "  \"drops_injected\": " << drops.dropped_a << ",\n";
  os << "  \"crc_match\": " << (crc_match ? "true" : "false") << ",\n";
  os << "  \"divergence_count\": " << div_count << "\n";
  os << "}\n";
  os.close();

  std::printf("journal=%s\n", cfg.journal_path.c_str());
  std::printf("output=%s\n", cfg.output_path.c_str());
  std::printf("events_processed=%llu\n", static_cast<unsigned long long>(source.size()));
  std::printf("drops_injected=%llu\n", static_cast<unsigned long long>(drops.dropped_a));
  std::printf("baseline_crc=%s\n", crc_hex(baseline.crc).c_str());
  std::printf("drop_injected_crc=%s\n", crc_hex(injected.crc).c_str());
  std::printf("crc_match=%s\n", crc_match ? "true" : "false");
  std::printf("divergence_count=%llu\n", static_cast<unsigned long long>(div_count));
  return crc_match ? 0 : 1;
#endif
}
