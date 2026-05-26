#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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

#include "mf/core/book_event_crc.hpp"
#include "mf/journal/journal_semantic_crc.hpp"
#include "mf/journal/journal_writer.hpp"
#include "mf/proto/nasdaq/itch50_parser.hpp"
#include "mf/transport/afxdp/feed_session.hpp"
#include "mf/wire/itch_moldudp.hpp"

namespace {

void be16(std::uint8_t* p, std::uint16_t v) {
  p[0] = static_cast<std::uint8_t>(v >> 8U);
  p[1] = static_cast<std::uint8_t>(v);
}

std::uint32_t offline_crc_from_messages(const std::vector<std::vector<std::uint8_t>>& messages) {
  mf::proto::nasdaq::Itch50Parser parser;
  mf::proto::nasdaq::ParseStats stats {};
  std::uint32_t crc = 0;
  std::uint64_t seq = 1;
  for (const auto& raw : messages) {
    if (raw.size() < 2) {
      continue;
    }
    const std::uint16_t msg_len =
        static_cast<std::uint16_t>((static_cast<std::uint16_t>(raw[0]) << 8U) | raw[1]);
    if (raw.size() < 2 + msg_len) {
      continue;
    }
    auto ev = parser.parse_message(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(raw.data() + 2), msg_len),
        seq++,
        seq + 1000,
        stats);
    if (!ev.has_value()) {
      continue;
    }
    mf::core::update_crc_from_book_event(crc, *ev);
  }
  return crc;
}

}  // namespace

int main() {
#if !defined(__linux__) || !defined(MF_HAS_LIBBPF)
  std::printf("SKIP: AF_XDP loopback test requires Linux + libbpf\n");
  return 0;
#else
  const char* ifname_env = std::getenv("MF_AF_XDP_IFNAME");
  const char* bpf_env = std::getenv("MF_AF_XDP_BPF_OBJ");
  const char* dst_ip_env = std::getenv("MF_AF_XDP_DST_IP");
  const std::string ifname = ifname_env != nullptr ? ifname_env : "veth1";
  const std::string bpf_obj = bpf_env != nullptr ? bpf_env : "build/afxdp_redirect.bpf.o";
  const std::string dst_ip = dst_ip_env != nullptr ? dst_ip_env : "10.200.1.2";
  const int dst_port = 5000;

  if (!std::filesystem::exists(bpf_obj)) {
    std::printf("SKIP: BPF object missing at %s (run scripts/setup_afxdp_netns.sh)\n", bpf_obj.c_str());
    return 0;
  }

  constexpr int kN = 500;
  std::vector<std::vector<std::uint8_t>> itch_messages;
  itch_messages.reserve(kN);
  for (int i = 0; i < kN; ++i) {
    std::vector<std::uint8_t> msg(2 + 1 + 36, 0);
    be16(msg.data(), 36);
    msg[2] = 'A';
    msg[14] = static_cast<std::uint8_t>((i + 1) & 0xFF);
    msg[23] = ((i & 1) == 0) ? 'B' : 'S';
    msg[27] = 10;
    msg[28] = 'A';
    msg[29] = 'A';
    msg[30] = 'P';
    msg[31] = 'L';
    msg[37] = static_cast<std::uint8_t>(10 + ((i + 1) % 8));
    itch_messages.push_back(std::move(msg));
  }
  const std::uint32_t offline_crc = offline_crc_from_messages(itch_messages);

  char journal_path[] = "/tmp/mf_afxdp_test_XXXXXX";
  const int tmp = ::mkstemp(journal_path);
  if (tmp < 0) {
    std::printf("SKIP: temp journal failed\n");
    return 0;
  }
  ::close(tmp);
  ::unlink(journal_path);

  mf::journal::JournalWriter writer;
  if (!writer.open(journal_path)) {
    std::printf("SKIP: journal open failed\n");
    return 0;
  }

  std::uint64_t callbacks = 0;
  mf::transport::afxdp::AfxdpConfig cfg {};
  cfg.ifname = ifname;
  cfg.bpf_obj_path = bpf_obj;
  mf::transport::afxdp::AfxdpFeedSession session(cfg, [&](const mf::core::BookEvent& ev) {
    writer.append(ev, ev.ingest_ts_ns);
    ++callbacks;
  });
  if (!session.open()) {
    std::printf("SKIP: AF_XDP session open failed (need root/netns)\n");
    ::unlink(journal_path);
    return 0;
  }

  const int tx = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (tx < 0) {
    std::printf("SKIP: tx socket failed\n");
    session.close();
    ::unlink(journal_path);
    return 0;
  }
  sockaddr_in dst {};
  dst.sin_family = AF_INET;
  dst.sin_port = htons(dst_port);
  ::inet_pton(AF_INET, dst_ip.c_str(), &dst.sin_addr);

  std::vector<std::uint8_t> blob;
  for (const auto& m : itch_messages) {
    blob.insert(blob.end(), m.begin(), m.end());
  }
  mf::wire::ItchMoldUdpFramer framer;
  std::size_t off = 0;
  while (off + 2 <= blob.size()) {
    std::size_t consumed = 0;
    auto datagram = framer.pack_datagram(blob.data() + off, blob.size() - off, consumed);
    if (datagram.empty()) {
      break;
    }
    (void)::sendto(tx, datagram.data(), datagram.size(), 0,
        reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
    off += consumed;
  }
  ::close(tx);

  for (int i = 0; i < 400 && callbacks < static_cast<std::uint64_t>(kN); ++i) {
    session.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  session.close();
  writer.close();

  mf::journal::JournalSemanticStats wire_stats {};
  assert(mf::journal::compute_semantic_crc(journal_path, wire_stats));
  assert(wire_stats.records == static_cast<std::uint64_t>(kN));
  assert(wire_stats.crc == offline_crc);
  assert(session.stats().parse_failures == 0);

  std::printf("PASS afxdp_loopback records=%llu semantic_crc=0x%08x\n",
      static_cast<unsigned long long>(wire_stats.records),
      wire_stats.crc);
  ::unlink(journal_path);
  return 0;
#endif
}
