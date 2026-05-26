#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "mf/transport/afxdp/config.hpp"

namespace mf::transport::afxdp {

struct AfxdpReceiverStats {
  std::uint64_t frames_received{0};
  std::uint64_t bytes_received{0};
  std::uint64_t udp_payloads{0};
  std::uint64_t parse_skips{0};
};

class AfxdpReceiver {
 public:
  explicit AfxdpReceiver(AfxdpConfig cfg);
  ~AfxdpReceiver();

  AfxdpReceiver(const AfxdpReceiver&) = delete;
  AfxdpReceiver& operator=(const AfxdpReceiver&) = delete;

  [[nodiscard]] bool available() const noexcept;
  [[nodiscard]] bool open();
  ssize_t recv(std::uint8_t* buf, std::size_t buflen, std::uint64_t& ingest_ts_ns);
  void close();
  [[nodiscard]] const AfxdpReceiverStats& stats() const noexcept;

 private:
  struct Impl;
  Impl* impl_{nullptr};
};

[[nodiscard]] bool attach_xdp_redirect(AfxdpConfig& cfg, int xsk_fd);

}  // namespace mf::transport::afxdp
