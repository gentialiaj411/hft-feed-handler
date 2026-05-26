#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "mf/bench/histogram.hpp"
#include "mf/bench/run_metadata.hpp"

#if defined(__linux__)
#include <fstream>
#endif
#include "mf/core/book_event_crc.hpp"
#include "mf/core/time.hpp"
#include "mf/journal/journal_semantic_crc.hpp"
#include "mf/journal/journal_writer.hpp"
#include "mf/proto/nasdaq/itch50_parser.hpp"
#include "mf/transport/afxdp/feed_session.hpp"
#include "mf/wire/itch_moldudp.hpp"

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

bool write_offline_journal(
    const std::string& itch_path,
    const std::string& journal_path,
    std::uint64_t max_events,
    std::uint32_t& semantic_crc,
    std::uint64_t& events_written) {
  std::ifstream in(itch_path, std::ios::binary);
  if (!in) {
    return false;
  }
  mf::journal::JournalWriter writer;
  if (std::filesystem::exists(journal_path)) {
    std::error_code ec;
    std::filesystem::remove(journal_path, ec);
  }
  if (!writer.open(journal_path)) {
    return false;
  }
  mf::proto::nasdaq::Itch50Parser parser;
  mf::proto::nasdaq::ParseStats stats {};
  std::array<std::uint8_t, 2> len_buf {};
  std::uint64_t seq = 1;
  semantic_crc = 0;
  events_written = 0;
  std::uint64_t last_exchange_ts_ns = 0;

  while (in.read(reinterpret_cast<char*>(len_buf.data()), len_buf.size())) {
    const std::uint16_t msg_len =
        static_cast<std::uint16_t>((static_cast<std::uint16_t>(len_buf[0]) << 8U) | len_buf[1]);
    if (msg_len == 0) {
      continue;
    }
    std::vector<std::byte> msg(msg_len);
    if (!in.read(reinterpret_cast<char*>(msg.data()), static_cast<std::streamsize>(msg_len))) {
      break;
    }
    auto ev = parser.parse_message(std::span<const std::byte>(msg.data(), msg.size()), seq++,
        mf::core::monotonic_raw_now_ns(), stats);
    if (!ev.has_value()) {
      continue;
    }
    if (ev->exchange_ts_ns == 0 && last_exchange_ts_ns != 0) {
      ev->exchange_ts_ns = last_exchange_ts_ns;
    } else if (ev->exchange_ts_ns != 0) {
      last_exchange_ts_ns = ev->exchange_ts_ns;
    }
    mf::core::update_crc_from_book_event(semantic_crc, *ev);
    writer.append(*ev, ev->ingest_ts_ns);
    ++events_written;
    if (max_events > 0 && events_written >= max_events) {
      break;
    }
  }
  writer.close();
  return true;
}
}  // namespace

int main(int argc, char** argv) {
#if !defined(__linux__)
  std::printf("afxdp_loopback_bench is Linux-only\n");
  return 0;
#else
  const std::string itch_path = arg(argc, argv, "--in", "");
  const std::string ifname = arg(argc, argv, "--ifname", "veth1");
  const std::string bpf_obj = arg(argc, argv, "--bpf-obj", "");
  const std::string dst_ip = arg(argc, argv, "--dst-ip", "10.200.1.2");
  const int dst_port = static_cast<int>(arg_u64(argc, argv, "--dst-port", 5000));
  const std::string out_md = arg(argc, argv, "--out-md", "bench/results/afxdp_loopback.md");
  const std::uint64_t max_events = arg_u64(argc, argv, "--max-events", 50000);
  const std::uint64_t duration_sec = arg_u64(argc, argv, "--duration-sec", 60);
  if (itch_path.empty() || bpf_obj.empty()) {
    std::fprintf(stderr,
        "usage: afxdp_loopback_bench --in <itch> --bpf-obj <path> [--ifname veth1] "
        "[--dst-ip 10.200.1.2] [--dst-port 5000] [--max-events N] [--duration-sec 60]\n");
    return 2;
  }

  const std::string offline_journal = "/tmp/mf_afxdp_offline.journal";
  const std::string afxdp_journal = "/tmp/mf_afxdp_wire.journal";
  std::uint32_t offline_crc = 0;
  std::uint64_t offline_events = 0;
  if (!write_offline_journal(itch_path, offline_journal, max_events, offline_crc, offline_events)) {
    std::fprintf(stderr, "offline journal generation failed\n");
    return 1;
  }

  mf::journal::JournalWriter writer;
  if (std::filesystem::exists(afxdp_journal)) {
    std::error_code ec;
    std::filesystem::remove(afxdp_journal, ec);
  }
  if (!writer.open(afxdp_journal)) {
    std::fprintf(stderr, "failed to open afxdp journal\n");
    return 1;
  }

  mf::bench::LatencyHistogram hist;
  std::uint64_t events_written = 0;
  mf::transport::afxdp::AfxdpConfig cfg {};
  cfg.ifname = ifname;
  cfg.bpf_obj_path = bpf_obj;
  mf::transport::afxdp::AfxdpFeedSession session(cfg, [&](const mf::core::BookEvent& ev) {
    writer.append(ev, ev.ingest_ts_ns);
    ++events_written;
  });
  if (!session.open()) {
    std::fprintf(stderr, "SKIP: AF_XDP open failed\n");
    writer.close();
    return 0;
  }

  const int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    std::fprintf(stderr, "tx socket failed\n");
    return 1;
  }
  sockaddr_in dst {};
  dst.sin_family = AF_INET;
  dst.sin_port = htons(static_cast<std::uint16_t>(dst_port));
  ::inet_pton(AF_INET, dst_ip.c_str(), &dst.sin_addr);

  std::ifstream in(itch_path, std::ios::binary);
  std::vector<std::uint8_t> file_buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  mf::wire::ItchMoldUdpFramer framer;
  std::size_t off = 0;
  const auto t0 = std::chrono::steady_clock::now();
  const auto deadline = t0 + std::chrono::seconds(duration_sec);
  std::uint64_t datagrams_sent = 0;

  while (off + 2 <= file_buf.size() && std::chrono::steady_clock::now() < deadline) {
    if (max_events > 0 && events_written >= max_events) {
      break;
    }
    std::size_t consumed = 0;
    auto datagram = framer.pack_datagram(file_buf.data() + off, file_buf.size() - off, consumed);
    if (datagram.empty()) {
      break;
    }
    const std::uint64_t wire_t0 = mf::core::monotonic_raw_now_ns();
    (void)::sendto(sock, datagram.data(), datagram.size(), 0,
        reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
    for (int spin = 0; spin < 1000; ++spin) {
      session.poll();
      if (session.stats().sink_callbacks_fired > events_written) {
        break;
      }
    }
    const std::uint64_t wire_t1 = mf::core::monotonic_raw_now_ns();
    if (wire_t1 > wire_t0) {
      hist.record(wire_t1 - wire_t0);
    }
    off += consumed;
    ++datagrams_sent;
  }
  ::close(sock);

  for (int i = 0; i < 5000; ++i) {
    session.poll();
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    if (events_written >= offline_events) {
      break;
    }
  }
  session.close();
  writer.close();

  const auto t1 = std::chrono::steady_clock::now();
  const double wall_sec = std::chrono::duration<double>(t1 - t0).count();
  const double mps = (wall_sec > 0.0) ? static_cast<double>(events_written) / wall_sec : 0.0;

  mf::journal::JournalSemanticStats wire_stats {};
  mf::journal::compute_semantic_crc(afxdp_journal, wire_stats);
  const bool crc_match = (wire_stats.crc == offline_crc) && (wire_stats.records == offline_events);

  const auto meta = mf::bench::capture_run_metadata(argc, argv);
  std::string veth_peer = "unknown";
#if defined(__linux__)
  {
    std::ifstream peer("/sys/class/net/" + ifname + "/peer_ifindex");
    if (peer) {
      std::string idx;
      peer >> idx;
      if (!idx.empty()) {
        const std::string path = "/sys/class/net/veth" + idx + "/ifalias";
        std::ifstream alias(path);
        if (alias) {
          std::getline(alias, veth_peer);
        } else {
          veth_peer = "veth peer ifindex " + idx;
        }
      }
    }
  }
#endif

  std::FILE* md = std::fopen(out_md.c_str(), "w");
  if (md != nullptr) {
    std::fprintf(md, "# AF_XDP loopback bench\n\n");
    std::fprintf(md, "**Status:** loopback netns only (not real NIC / not DPDK).\n\n");
    std::fprintf(md, "## Host\n\n");
    std::fprintf(md, "| field | value |\n|---|---|\n");
    std::fprintf(md, "| host | %s |\n", meta.host.c_str());
    std::fprintf(md, "| kernel | %s |\n", meta.kernel.c_str());
    std::fprintf(md, "| cpu_model | %s |\n", meta.cpu_model.c_str());
    std::fprintf(md, "| cpu_count | %d |\n", meta.cpu_count);
    std::fprintf(md, "| utc_timestamp | %s |\n", meta.utc_timestamp.c_str());
    std::fprintf(md, "| consumer_ifname | %s |\n", ifname.c_str());
    std::fprintf(md, "| veth_peer | %s |\n", veth_peer.c_str());
    std::fprintf(md, "| itch_input | %s |\n", itch_path.c_str());
    std::fprintf(md, "\nEnvironment: netns from `docs/runbook-afxdp.md`. Pinning: `docs/runbook-pinning.md` (optional).\n\n");
    std::fprintf(md, "| metric | value |\n|---|---|\n");
    std::fprintf(md, "| duration_sec | %.3f |\n", wall_sec);
    std::fprintf(md, "| datagrams_sent | %llu |\n", static_cast<unsigned long long>(datagrams_sent));
    std::fprintf(md, "| events_offline | %llu |\n", static_cast<unsigned long long>(offline_events));
    std::fprintf(md, "| events_afxdp | %llu |\n", static_cast<unsigned long long>(wire_stats.records));
    std::fprintf(md, "| sustained_msg_per_sec | %.2f |\n", mps);
    std::fprintf(md, "| p50_wire_to_decode_ns | %llu |\n", static_cast<unsigned long long>(hist.percentile(0.50)));
    std::fprintf(md, "| p99_wire_to_decode_ns | %llu |\n", static_cast<unsigned long long>(hist.percentile(0.99)));
    std::fprintf(md, "| offline_semantic_crc | 0x%08x |\n", offline_crc);
    std::fprintf(md, "| afxdp_semantic_crc | 0x%08x |\n", wire_stats.crc);
    std::fprintf(md, "| journal_crc_matches_offline | %s |\n", crc_match ? "true" : "false");
    std::fclose(md);
  }

  std::printf("events_offline=%llu events_afxdp=%llu mps=%.2f p50=%llu p99=%llu crc_match=%d\n",
      static_cast<unsigned long long>(offline_events),
      static_cast<unsigned long long>(wire_stats.records),
      mps,
      static_cast<unsigned long long>(hist.percentile(0.50)),
      static_cast<unsigned long long>(hist.percentile(0.99)),
      crc_match ? 1 : 0);
  return crc_match ? 0 : 5;
#endif
}
