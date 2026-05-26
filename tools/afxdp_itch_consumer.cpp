#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>

#include "mf/journal/journal_semantic_crc.hpp"
#include "mf/journal/journal_writer.hpp"
#include "mf/transport/afxdp/feed_session.hpp"

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

void usage() {
  std::fprintf(stderr,
      "usage: afxdp_itch_consumer --ifname <iface> --bpf-obj <path> --out <journal> "
      "[--queue-id N] [--wait-ms N] [--expected-events N]\n");
}
}  // namespace

int main(int argc, char** argv) {
#if !defined(__linux__)
  std::printf("afxdp_itch_consumer is Linux-only\n");
  return 0;
#else
  const std::string ifname = arg(argc, argv, "--ifname", "veth1");
  const std::string bpf_obj = arg(argc, argv, "--bpf-obj", "");
  const std::string out_path = arg(argc, argv, "--out", "");
  const std::uint32_t queue_id = static_cast<std::uint32_t>(arg_u64(argc, argv, "--queue-id", 0));
  const std::uint64_t wait_ms = arg_u64(argc, argv, "--wait-ms", 30000);
  const std::uint64_t expected = arg_u64(argc, argv, "--expected-events", 0);
  if (out_path.empty() || bpf_obj.empty()) {
    usage();
    return 2;
  }

  mf::journal::JournalWriter writer;
  if (!writer.open(out_path)) {
    std::fprintf(stderr, "failed to open journal: %s\n", out_path.c_str());
    return 1;
  }

  std::uint64_t events_written = 0;
  mf::transport::afxdp::AfxdpConfig cfg {};
  cfg.ifname = ifname;
  cfg.queue_id = queue_id;
  cfg.bpf_obj_path = bpf_obj;
  cfg.bind_flags_copy = true;

  mf::transport::afxdp::AfxdpFeedSession session(cfg, [&](const mf::core::BookEvent& ev) {
    writer.append(ev, ev.ingest_ts_ns);
    ++events_written;
  });

  if (!session.open()) {
    std::fprintf(stderr, "SKIP: AF_XDP session open failed (need root + libbpf + netns setup)\n");
    writer.close();
    return 0;
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    session.poll();
    if (expected > 0 && events_written >= expected) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  session.close();
  writer.close();

  mf::journal::JournalSemanticStats stats {};
  if (!mf::journal::compute_semantic_crc(out_path, stats)) {
    std::fprintf(stderr, "journal semantic crc failed\n");
    return 3;
  }

  std::printf("journal=%s events_written=%llu semantic_crc=0x%08x parse_failures=%llu udp_payloads=%llu\n",
      out_path.c_str(),
      static_cast<unsigned long long>(events_written),
      stats.crc,
      static_cast<unsigned long long>(session.stats().parse_failures),
      static_cast<unsigned long long>(session.receiver_stats().udp_payloads));
  if (expected > 0 && events_written < expected) {
    std::fprintf(stderr, "incomplete: expected=%llu got=%llu\n",
        static_cast<unsigned long long>(expected),
        static_cast<unsigned long long>(events_written));
    return 4;
  }
  return 0;
#endif
}
