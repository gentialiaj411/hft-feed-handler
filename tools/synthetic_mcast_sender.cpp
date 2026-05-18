#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "mf/proto/iex/deep_messages.hpp"
#include "mf/proto/nasdaq/itch50_messages.hpp"

namespace {
void be16(std::uint8_t* p, std::uint16_t v) { p[0] = static_cast<std::uint8_t>(v >> 8U); p[1] = static_cast<std::uint8_t>(v); }
void be64(std::uint8_t* p, std::uint64_t v) { for (int i = 7; i >= 0; --i) p[7 - i] = static_cast<std::uint8_t>((v >> (8U * i)) & 0xFFU); }
std::string arg(int argc, char** argv, const std::string& k, const std::string& d) { for (int i = 1; i + 1 < argc; ++i) if (argv[i] == k) return argv[i + 1]; return d; }
double argd(int argc, char** argv, const std::string& k, double d) { return std::stod(arg(argc, argv, k, std::to_string(d))); }
std::uint64_t argu64(int argc, char** argv, const std::string& k, std::uint64_t d) { return std::stoull(arg(argc, argv, k, std::to_string(d))); }

std::vector<std::uint8_t> make_nasdaq_datagram(std::uint64_t seq) {
  std::vector<std::uint8_t> out(20);
  std::memcpy(out.data(), "SESSION0001", 10);
  be64(out.data() + 10, seq);
  be16(out.data() + 18, 1);
  mf::proto::nasdaq::AddOrderMessage m{};
  m.timestamp[5] = static_cast<std::uint8_t>(seq & 0xFFU);
  m.order_ref_be = __builtin_bswap64(seq);
  m.buy_sell = 'B';
  m.shares_be = __builtin_bswap32(100);
  m.stock = {'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '};
  m.price_be = __builtin_bswap32(10000U + static_cast<std::uint32_t>(seq % 1000U));
  out.resize(22 + 1 + sizeof(m));
  be16(out.data() + 20, static_cast<std::uint16_t>(1 + sizeof(m)));
  out[22] = static_cast<std::uint8_t>('A');
  std::memcpy(out.data() + 23, &m, sizeof(m));
  return out;
}

std::vector<std::uint8_t> make_iex_datagram(std::uint64_t seq) {
  mf::proto::iex::AddOrderMessage m{};
  m.msg_type = 'a'; m.side = '8'; m.timestamp_le = seq;
  m.symbol = {'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '};
  m.order_id_le = seq; m.size_le = 100; m.price_le = 10000 + seq;
  const std::uint16_t payload_len = static_cast<std::uint16_t>(2 + sizeof(m));
  std::vector<std::uint8_t> out(36 + payload_len);
  out[0] = 1; out[1] = 1; be16(out.data() + 2, 1); out[7] = 1;
  be16(out.data() + 8, payload_len); be16(out.data() + 10, 1);
  be64(out.data() + 12, seq); be64(out.data() + 20, seq); be64(out.data() + 28, seq);
  be16(out.data() + 36, static_cast<std::uint16_t>(sizeof(m)));
  std::memcpy(out.data() + 38, &m, sizeof(m));
  return out;
}

std::vector<std::uint8_t> make_pitch_datagram(std::uint64_t seq) {
  char line[64];
  std::snprintf(line, sizeof(line), "%08lluA000000000001B000100AAPL  0000010000Z\n", static_cast<unsigned long long>(seq % 100000000ULL));
  return std::vector<std::uint8_t>(line, line + std::strlen(line));
}
}  // namespace

int main(int argc, char** argv) {
  const std::string protocol = arg(argc, argv, "--protocol", "nasdaq");
  const std::string group = arg(argc, argv, "--group", "239.0.0.42");
  const int port = static_cast<int>(argu64(argc, argv, "--port", 31337));
  const std::string iface = arg(argc, argv, "--iface", "127.0.0.1");
  const std::uint64_t total = argu64(argc, argv, "--total-events", 100000);
  const std::uint64_t rate = argu64(argc, argv, "--rate-mps", 100000);
  const double drop_a = argd(argc, argv, "--drop-rate-a", 0.0);
  const double drop_b = argd(argc, argv, "--drop-rate-b", 0.0);
  const std::uint64_t jitter_us = argu64(argc, argv, "--jitter-us", 0);
  const std::uint64_t seed = argu64(argc, argv, "--seed", 1);

  const int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) return 1;
  in_addr iface_addr{}; iface_addr.s_addr = ::inet_addr(iface.c_str());
  ::setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &iface_addr, sizeof(iface_addr));
  sockaddr_in dst_a{}; dst_a.sin_family = AF_INET; dst_a.sin_port = htons(static_cast<std::uint16_t>(port)); dst_a.sin_addr.s_addr = ::inet_addr(group.c_str());
  sockaddr_in dst_b = dst_a; dst_b.sin_port = htons(static_cast<std::uint16_t>(port + 1));

  std::mt19937_64 rng(seed);
  std::uniform_real_distribution<double> u(0.0, 1.0);
  std::uniform_int_distribution<int> jdist(0, static_cast<int>(jitter_us));

  for (std::uint64_t seq = 1; seq <= total; ++seq) {
    std::vector<std::uint8_t> pkt = (protocol == "iex") ? make_iex_datagram(seq) : ((protocol == "cboe") ? make_pitch_datagram(seq) : make_nasdaq_datagram(seq));
    if (u(rng) >= drop_a) (void)::sendto(sock, pkt.data(), pkt.size(), 0, reinterpret_cast<sockaddr*>(&dst_a), sizeof(dst_a));
    if (u(rng) >= drop_b) (void)::sendto(sock, pkt.data(), pkt.size(), 0, reinterpret_cast<sockaddr*>(&dst_b), sizeof(dst_b));
    if (jitter_us > 0) std::this_thread::sleep_for(std::chrono::microseconds(jdist(rng)));
    if (rate > 0) std::this_thread::sleep_for(std::chrono::nanoseconds(1000000000ULL / rate));
  }
  ::close(sock);
  return 0;
}
