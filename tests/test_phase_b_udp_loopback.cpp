#include <cassert>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "mf/wire/event_loop.hpp"
#include "mf/wire/feed_session.hpp"

namespace {
void be16(std::uint8_t* p, std::uint16_t v) { p[0] = static_cast<std::uint8_t>(v >> 8U); p[1] = static_cast<std::uint8_t>(v); }
void be64(std::uint8_t* p, std::uint64_t v) { for (int i = 7; i >= 0; --i) p[7 - i] = static_cast<std::uint8_t>((v >> (8U * i)) & 0xFFU); }
std::vector<std::uint8_t> make_pkt(std::uint64_t seq) {
  std::vector<std::uint8_t> d(20 + 2 + 36, 0);
  std::memcpy(d.data(), "SESSION0001", 10);
  be64(d.data() + 10, seq); be16(d.data() + 18, 1); be16(d.data() + 20, 36);
  d[22] = 'A';
  d[34] = static_cast<std::uint8_t>(seq & 0xFFU);  // order id LSB in BE u64
  d[43] = 'B';                                      // side
  d[47] = 100;                                      // qty LSB in BE u32
  d[48] = 'A'; d[49] = 'A'; d[50] = 'P'; d[51] = 'L';
  d[57] = 10;                                       // price LSB in BE u32
  return d;
}
}  // namespace

int main() {
  const char* group = "239.0.0.42";
  const int port = 31337;
  std::uint64_t callbacks = 0;
  mf::wire::FeedSession s({group, static_cast<std::uint16_t>(port), "127.0.0.1"}, mf::wire::WireProtocol::NasdaqItch50, mf::phase2::FeedSide::A,
      [&](mf::phase2::FeedSide, const mf::core::BookEvent&) { ++callbacks; });
  if (!s.open()) {
    std::printf("SKIP: multicast receiver open failed\n");
    return 0;
  }

  int tx = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (tx < 0) {
    std::printf("SKIP: tx socket failed\n");
    return 0;
  }
  in_addr iface{}; iface.s_addr = ::inet_addr("127.0.0.1");
  if (::setsockopt(tx, IPPROTO_IP, IP_MULTICAST_IF, &iface, sizeof(iface)) != 0) {
    std::printf("SKIP: IP_MULTICAST_IF unavailable\n");
    ::close(tx);
    return 0;
  }
  sockaddr_in dst{}; dst.sin_family = AF_INET; dst.sin_port = htons(port); dst.sin_addr.s_addr = ::inet_addr(group);
  constexpr int N = 10000;
  for (int i = 0; i < N; ++i) {
    auto p = make_pkt(static_cast<std::uint64_t>(i + 1));
    (void)::sendto(tx, p.data(), p.size(), 0, reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
  }
  ::close(tx);
  mf::wire::WireEventLoop loop;
  assert(loop.add_session(&s));
  for (int i = 0; i < 200 && callbacks < N; ++i) {
    loop.run_for(std::chrono::milliseconds(5));
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(callbacks == N);
  assert(s.stats().parse_failures == 0);
  return 0;
}
