#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

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

void usage() {
  std::fprintf(stderr,
      "usage: afxdp_itch_producer --in <itch_path> --dst-ip <ip> --dst-port <port> "
      "[--max-events N] [--max-bytes N]\n");
}
}  // namespace

int main(int argc, char** argv) {
#if !defined(__linux__)
  std::printf("afxdp_itch_producer is Linux-only\n");
  return 0;
#else
  const std::string in_path = arg(argc, argv, "--in", "");
  const std::string dst_ip = arg(argc, argv, "--dst-ip", "10.200.1.2");
  const int dst_port = static_cast<int>(arg_u64(argc, argv, "--dst-port", 5000));
  const std::uint64_t max_events = arg_u64(argc, argv, "--max-events", 0);
  const std::uint64_t max_bytes = arg_u64(argc, argv, "--max-bytes", 0);
  if (in_path.empty()) {
    usage();
    return 2;
  }

  std::ifstream in(in_path, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "failed to open input: %s\n", in_path.c_str());
    return 1;
  }

  const int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    std::fprintf(stderr, "socket failed\n");
    return 1;
  }

  sockaddr_in dst {};
  dst.sin_family = AF_INET;
  dst.sin_port = htons(static_cast<std::uint16_t>(dst_port));
  if (::inet_pton(AF_INET, dst_ip.c_str(), &dst.sin_addr) != 1) {
    std::fprintf(stderr, "invalid dst-ip: %s\n", dst_ip.c_str());
    ::close(sock);
    return 2;
  }

  std::vector<std::uint8_t> file_buf;
  file_buf.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  if (max_bytes > 0 && file_buf.size() > max_bytes) {
    file_buf.resize(static_cast<std::size_t>(max_bytes));
  }

  mf::wire::ItchMoldUdpFramer framer;
  std::size_t off = 0;
  std::uint64_t messages_sent = 0;
  std::uint64_t datagrams_sent = 0;

  while (off + 2 <= file_buf.size()) {
    std::size_t consumed = 0;
    auto datagram = framer.pack_datagram(file_buf.data(), file_buf.size() - off, consumed);
    if (datagram.empty()) {
      break;
    }
    (void)::sendto(sock, datagram.data(), datagram.size(), 0,
        reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
    ++datagrams_sent;
    off += consumed;
    messages_sent += consumed > 0 ? 1 : 0;
    if (max_events > 0 && messages_sent >= max_events) {
      break;
    }
  }

  ::close(sock);
  std::printf("datagrams_sent=%llu bytes_consumed=%zu\n",
      static_cast<unsigned long long>(datagrams_sent),
      off);
  return 0;
#endif
}
