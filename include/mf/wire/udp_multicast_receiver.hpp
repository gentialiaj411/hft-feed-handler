#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#ifdef __linux__
#include <sys/types.h>
#endif

namespace mf::wire {

// Non-blocking receiver supports epoll edge-triggered loops by ensuring recv drains to EAGAIN.
// Edge-triggered avoids repeated wakeups when no new packets arrive.
// SO_RCVBUF is explicitly sized because default kernel socket buffers are often too small.
// We record effective receive buffer because Linux doubles requested values internally.
// This path exposes UDP loss at the kernel ring when application drain rate falls behind.
// Loss can also happen due to NIC/driver ring overflow before userspace sees datagrams.
// The receiver performs single-datagram reads to preserve message boundaries.
// Ingest timestamp is captured around recv call using monotonic raw nanoseconds.
// No heap allocation occurs in steady-state recv path.
// Linux-only implementation in src/wire/udp_multicast_receiver.cpp.
struct McastReceiverConfig {
  std::string group_ip{};
  std::uint16_t port{0};
  std::string iface_ip{};
  int rcvbuf_bytes{8 * 1024 * 1024};
  bool reuse_port{true};
};

class UdpMulticastReceiver {
 public:
  explicit UdpMulticastReceiver(McastReceiverConfig cfg) : cfg_(std::move(cfg)) {}
  ~UdpMulticastReceiver();

  bool open();
  [[nodiscard]] int fd() const { return fd_; }
  ssize_t recv(std::uint8_t* buf, std::size_t buflen, std::uint64_t& ingest_ts_ns);
  void close();
  [[nodiscard]] int effective_rcvbuf_bytes() const noexcept { return effective_rcvbuf_bytes_; }

 private:
  McastReceiverConfig cfg_{};
  int fd_{-1};
  int effective_rcvbuf_bytes_{0};
};

}  // namespace mf::wire
