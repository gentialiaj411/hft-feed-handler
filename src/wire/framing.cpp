#include "mf/wire/framing.hpp"

#include <algorithm>

namespace {

std::uint16_t be16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8U) | p[1]);
}
std::uint64_t be64(const std::uint8_t* p) {
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v = (v << 8U) | p[i];
  return v;
}
std::uint32_t be32(const std::uint8_t* p) {
  return (static_cast<std::uint32_t>(p[0]) << 24U) |
         (static_cast<std::uint32_t>(p[1]) << 16U) |
         (static_cast<std::uint32_t>(p[2]) << 8U) |
         static_cast<std::uint32_t>(p[3]);
}
std::uint64_t parse_ascii_u64(const std::uint8_t* p, std::size_t n) {
  std::uint64_t v = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const char c = static_cast<char>(p[i]);
    if (c < '0' || c > '9') return 0;
    v = (v * 10ULL) + static_cast<std::uint64_t>(c - '0');
  }
  return v;
}

}  // namespace

namespace mf::wire {

void DatagramFramer::frame(const std::uint8_t* datagram, std::size_t len, std::function<void(FrameSlice)> on_frame) const {
  if (datagram == nullptr || len == 0) return;

  if (protocol_ == WireProtocol::NasdaqItch50) {
    // MoldUDP64: [session:10][sequence:8][message_count:2] then repeated [msg_len:2][payload].
    if (len < 20) return;
    const std::uint64_t first_seq = be64(datagram + 10);
    const std::uint16_t msg_count = be16(datagram + 18);
    std::size_t off = 20;
    for (std::uint16_t i = 0; i < msg_count && off + 2 <= len; ++i) {
      const std::uint16_t mlen = be16(datagram + off);
      off += 2;
      if (off + mlen > len) return;
      on_frame(FrameSlice{datagram + off, mlen, first_seq + i});
      off += mlen;
    }
    return;
  }

  if (protocol_ == WireProtocol::IexDeep) {
    // IEX-TP v1-style framing (network order here): v(1), proto(1), channel(2), session(4),
    // payload_len(2), msg_count(2), stream_offset(8), first_msg_seq(8), send_time(8), payload...
    if (len < 36) return;
    const std::uint16_t payload_len = be16(datagram + 8);
    const std::uint16_t msg_count = be16(datagram + 10);
    const std::uint64_t first_seq = be64(datagram + 20);
    const std::size_t payload_off = 36;
    if (payload_off + payload_len > len) return;
    std::size_t off = payload_off;
    for (std::uint16_t i = 0; i < msg_count && off + 2 <= payload_off + payload_len; ++i) {
      const std::uint16_t mlen = be16(datagram + off);
      off += 2;
      if (off + mlen > payload_off + payload_len) return;
      on_frame(FrameSlice{datagram + off, mlen, first_seq + i});
      off += mlen;
    }
    return;
  }

  // Cboe PITCH here is newline-delimited ASCII in datagrams.
  std::size_t start = 0;
  for (std::size_t i = 0; i < len; ++i) {
    if (datagram[i] != static_cast<std::uint8_t>('\n')) continue;
    if (i > start) {
      const std::uint8_t* msg = datagram + start;
      const std::size_t mlen = i - start;
      const std::uint64_t seq = (mlen >= 8) ? parse_ascii_u64(msg, 8) : 0;
      on_frame(FrameSlice{msg, mlen, seq});
    }
    start = i + 1;
  }
  if (start < len) {
    const std::uint8_t* msg = datagram + start;
    const std::size_t mlen = len - start;
    const std::uint64_t seq = (mlen >= 8) ? parse_ascii_u64(msg, 8) : 0;
    on_frame(FrameSlice{msg, mlen, seq});
  }
}

}  // namespace mf::wire
