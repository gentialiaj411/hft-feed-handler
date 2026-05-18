#include "mf/wire/udp_multicast_receiver.hpp"

#ifdef __linux__
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "mf/core/time.hpp"

namespace mf::wire {

UdpMulticastReceiver::~UdpMulticastReceiver() { close(); }

bool UdpMulticastReceiver::open() {
  close();
  fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd_ < 0) return false;

  const int flags = ::fcntl(fd_, F_GETFL, 0);
  if (flags < 0 || ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
    close();
    return false;
  }

  int one = 1;
  if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0) {
    close();
    return false;
  }
  if (cfg_.reuse_port) {
#ifdef SO_REUSEPORT
    (void)::setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif
  }
  (void)::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &cfg_.rcvbuf_bytes, sizeof(cfg_.rcvbuf_bytes));

  sockaddr_in bind_addr{};
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_port = htons(cfg_.port);
  bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (::bind(fd_, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) != 0) {
    close();
    return false;
  }

  ip_mreq req{};
  req.imr_multiaddr.s_addr = ::inet_addr(cfg_.group_ip.c_str());
  req.imr_interface.s_addr = ::inet_addr(cfg_.iface_ip.c_str());
  if (::setsockopt(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &req, sizeof(req)) != 0) {
    close();
    return false;
  }

  socklen_t optlen = sizeof(effective_rcvbuf_bytes_);
  if (::getsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &effective_rcvbuf_bytes_, &optlen) != 0) {
    effective_rcvbuf_bytes_ = 0;
  }
  return true;
}

ssize_t UdpMulticastReceiver::recv(std::uint8_t* buf, std::size_t buflen, std::uint64_t& ingest_ts_ns) {
  const std::uint64_t t0 = mf::core::monotonic_raw_now_ns();
  const ssize_t n = ::recvfrom(fd_, buf, buflen, 0, nullptr, nullptr);
  const std::uint64_t t1 = mf::core::monotonic_raw_now_ns();
  ingest_ts_ns = (t0 <= t1) ? ((t0 + t1) / 2ULL) : t1;
  return n;
}

void UdpMulticastReceiver::close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

}  // namespace mf::wire
#endif
