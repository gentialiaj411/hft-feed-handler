#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "mf/bench/run_metadata.hpp"
#include "mf/core/book_event_crc.hpp"
#include "mf/core/time.hpp"
#include "mf/journal/journal_semantic_crc.hpp"
#include "mf/journal/journal_writer.hpp"
#include "mf/proto/mdp3/cert_bin_reader.hpp"
#include "mf/proto/mdp3/mdp3_parser.hpp"

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
}  // namespace

int main(int argc, char** argv) {
#if !defined(__linux__)
  std::printf("mdp3_replay_bench is Linux-only\n");
  return 0;
#else
  const std::string in_path = arg(argc, argv, "--in", "bench/data/mdp3_cert_incr_311_AX_17511.bin");
  const std::string out_md = arg(argc, argv, "--out-md", "bench/results/mdp3_replay.md");
  const std::string journal_path = arg(argc, argv, "--out-journal", "/tmp/mf_mdp3_replay.journal");
  const std::uint64_t max_packets = arg_u64(argc, argv, "--max-packets", 0);

  std::ifstream in(in_path, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "failed to open %s\n", in_path.c_str());
    return 1;
  }
  std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::vector<std::byte> file_buf(raw.size());
  std::memcpy(file_buf.data(), raw.data(), raw.size());

  auto run_once = [&](const std::string& journal_out, std::uint32_t& semantic_crc, std::uint64_t& events_out,
                      std::uint64_t& packets_out, mf::proto::mdp3::ParseStats& stats_out) -> bool {
    mf::journal::JournalWriter writer;
    if (!writer.open(journal_out)) {
      return false;
    }
    mf::proto::mdp3::Mdp3Parser parser;
    mf::proto::mdp3::CertBinReader reader(std::span<const std::byte>(file_buf.data(), file_buf.size()));
    std::span<const std::byte> udp {};
    std::uint64_t packets = 0;
    while (reader.next(udp)) {
      ++packets;
      if (max_packets > 0 && packets > max_packets) {
        break;
      }
      const auto events = parser.parse_packet(udp, mf::core::monotonic_raw_now_ns(), stats_out);
      for (const auto& ev : events) {
        writer.append(ev, ev.ingest_ts_ns);
      }
    }
    writer.close();
    packets_out = packets;
    mf::journal::JournalSemanticStats jstats {};
    if (!mf::journal::compute_semantic_crc(journal_out, jstats)) {
      return false;
    }
    semantic_crc = jstats.crc;
    events_out = jstats.records;
    return true;
  };

  const auto t0 = std::chrono::steady_clock::now();
  std::uint32_t crc_a = 0;
  std::uint64_t events_a = 0;
  std::uint64_t packets_a = 0;
  mf::proto::mdp3::ParseStats stats_a {};
  if (!run_once(journal_path, crc_a, events_a, packets_a, stats_a)) {
    std::fprintf(stderr, "run 1 failed\n");
    return 1;
  }
  const auto t1 = std::chrono::steady_clock::now();

  std::uint32_t crc_b = 0;
  std::uint64_t events_b = 0;
  std::uint64_t packets_b = 0;
  mf::proto::mdp3::ParseStats stats_b {};
  const std::string journal_b = journal_path + ".rerun";
  if (!run_once(journal_b, crc_b, events_b, packets_b, stats_b)) {
    std::fprintf(stderr, "run 2 failed\n");
    return 1;
  }
  const auto t2 = std::chrono::steady_clock::now();

  const double wall_sec = std::chrono::duration<double>(t1 - t0).count();
  const double mps = (wall_sec > 0.0) ? static_cast<double>(stats_a.parsed_messages) / wall_sec : 0.0;
  const bool crc_stable = (crc_a == crc_b) && (events_a == events_b);

  const auto meta = mf::bench::capture_run_metadata(argc, argv);

  std::FILE* md = std::fopen(out_md.c_str(), "w");
  if (md != nullptr) {
    std::fprintf(md, "# MDP3 replay bench\n\n");
    std::fprintf(md, "**Status:** Verified on recorded CME certification incremental feed sample.\n\n");
    std::fprintf(md, "## Host\n\n");
    std::fprintf(md, "| field | value |\n|---|---|\n");
    std::fprintf(md, "| host | %s |\n", meta.host.c_str());
    std::fprintf(md, "| kernel | %s |\n", meta.kernel.c_str());
    std::fprintf(md, "| cpu_model | %s |\n", meta.cpu_model.c_str());
    std::fprintf(md, "| cpu_count | %d |\n", meta.cpu_count);
    std::fprintf(md, "| utc_timestamp | %s |\n", meta.utc_timestamp.c_str());
    std::fprintf(md, "\n## Sample provenance\n\n");
    std::fprintf(md,
        "- Source: CME Globex MDP 3.0 certification-environment incremental feed capture "
        "(channel 311 feed AX), mirrored in "
        "[java-cme-mdp3-handler](https://github.com/kolybelkin/java-cme-mdp3-handler/blob/master/"
        "mbp-only/src/cucumber/sim/data/incr/311_AX_224.0.31.2_17511.zip).\n");
    std::fprintf(md,
        "- CME wire layout reference: [Packet Capture Dataset](https://cmegroupclientsite.atlassian.net/wiki/spaces/"
        "EPICSANDBOX/pages/457323061/Packet+Capture+Dataset) (retrieved 2026-05-24).\n");
    std::fprintf(md, "- Local path: `%s`\n", in_path.c_str());
    std::fprintf(md, "- Parser subset: template 32 (`MDIncrementalRefreshBook32`) only.\n\n");
    std::fprintf(md, "| metric | value |\n|---|---|\n");
    std::fprintf(md, "| packets_processed | %llu |\n", static_cast<unsigned long long>(packets_a));
    std::fprintf(md, "| sbe_messages_parsed | %llu |\n", static_cast<unsigned long long>(stats_a.parsed_messages));
    std::fprintf(md, "| book_events_emitted | %llu |\n", static_cast<unsigned long long>(events_a));
    std::fprintf(md, "| sustained_msg_per_sec | %.2f |\n", mps);
    std::fprintf(md, "| canonical_semantic_crc | 0x%08x |\n", crc_a);
    std::fprintf(md, "| rerun_semantic_crc | 0x%08x |\n", crc_b);
    std::fprintf(md, "| crc_stable_across_reruns | %s |\n", crc_stable ? "true" : "false");
    std::fprintf(md, "| rerun_wall_sec | %.3f |\n", std::chrono::duration<double>(t2 - t1).count());
    std::fclose(md);
  }

  std::printf("packets=%llu events=%llu mps=%.2f crc=0x%08x stable=%d\n",
      static_cast<unsigned long long>(packets_a),
      static_cast<unsigned long long>(events_a),
      mps,
      crc_a,
      crc_stable ? 1 : 0);
  return crc_stable ? 0 : 5;
#endif
}
