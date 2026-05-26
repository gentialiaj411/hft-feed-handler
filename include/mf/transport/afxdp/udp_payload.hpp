#pragma once

#include <cstddef>
#include <cstdint>

namespace mf::transport::afxdp {

// Extract UDP payload from a received L2 frame (Ethernet II + IPv4 + UDP).
// Returns payload length or 0 if parsing fails.
[[nodiscard]] std::size_t extract_udp_payload(
    const std::uint8_t* frame,
    std::size_t frame_len,
    const std::uint8_t*& payload_out) noexcept;

}  // namespace mf::transport::afxdp
