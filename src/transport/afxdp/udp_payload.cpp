#include "mf/transport/afxdp/udp_payload.hpp"

namespace mf::transport::afxdp {

namespace {
constexpr std::size_t kEthHeader = 14;
constexpr std::size_t kMinIpv4 = 20;
constexpr std::size_t kUdpHeader = 8;
}  // namespace

std::size_t extract_udp_payload(
    const std::uint8_t* frame,
    const std::size_t frame_len,
    const std::uint8_t*& payload_out) noexcept {
  payload_out = nullptr;
  if (frame == nullptr || frame_len < kEthHeader + kMinIpv4 + kUdpHeader) {
    return 0;
  }

  const std::uint16_t ethertype =
      static_cast<std::uint16_t>((static_cast<std::uint16_t>(frame[12]) << 8U) | frame[13]);
  if (ethertype != 0x0800U) {
    return 0;
  }

  const std::uint8_t* ip = frame + kEthHeader;
  const std::uint8_t version = static_cast<std::uint8_t>(ip[0] >> 4U);
  if (version != 4) {
    return 0;
  }
  const std::size_t ihl = static_cast<std::size_t>(ip[0] & 0x0FU) * 4U;
  if (ihl < kMinIpv4 || frame_len < kEthHeader + ihl + kUdpHeader) {
    return 0;
  }
  if (ip[9] != 17) {
    return 0;
  }

  const std::uint8_t* udp = ip + ihl;
  const std::size_t udp_len =
      static_cast<std::size_t>((static_cast<std::size_t>(udp[4]) << 8U) | udp[5]);
  if (udp_len < kUdpHeader) {
    return 0;
  }
  const std::size_t payload_len = udp_len - kUdpHeader;
  if (frame_len < kEthHeader + ihl + udp_len) {
    return 0;
  }

  payload_out = udp + kUdpHeader;
  return payload_len;
}

}  // namespace mf::transport::afxdp
