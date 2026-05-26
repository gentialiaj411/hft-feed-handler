#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace mf::wire {

// Pack raw ITCH length-prefixed messages into MoldUDP64 datagram payloads.
class ItchMoldUdpFramer {
 public:
  explicit ItchMoldUdpFramer(std::string session = "SESSION0001") : session_(std::move(session)) {}

  void reset_sequence(std::uint64_t seq = 1) noexcept { next_seq_ = seq; }

  // Returns one MoldUDP64 datagram containing up to max_messages_per_datagram ITCH payloads.
  // Empty vector means no complete datagram could be formed from remaining input.
  [[nodiscard]] std::vector<std::uint8_t> pack_datagram(
      const std::uint8_t* data,
      std::size_t len,
      std::size_t& consumed,
      std::size_t max_messages_per_datagram = 16,
      std::size_t max_datagram_bytes = 1400) const;

 private:
  static void write_be16(std::uint8_t* p, std::uint16_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v >> 8U);
    p[1] = static_cast<std::uint8_t>(v);
  }
  static void write_be64(std::uint8_t* p, std::uint64_t v) noexcept {
    for (int i = 7; i >= 0; --i) {
      p[7 - i] = static_cast<std::uint8_t>((v >> (8U * static_cast<unsigned>(i))) & 0xFFU);
    }
  }
  static std::uint16_t read_be16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8U) | p[1]);
  }

  std::string session_{};
  mutable std::uint64_t next_seq_{1};
};

inline std::vector<std::uint8_t> ItchMoldUdpFramer::pack_datagram(
    const std::uint8_t* data,
    std::size_t len,
    std::size_t& consumed,
    std::size_t max_messages_per_datagram,
    std::size_t max_datagram_bytes) const {
  consumed = 0;
  if (data == nullptr || len < 2) {
    return {};
  }

  std::vector<std::uint8_t> out{};
  out.resize(20);
  const std::size_t session_len = std::min<std::size_t>(session_.size(), 10);
  std::memset(out.data(), ' ', 10);
  std::memcpy(out.data(), session_.data(), session_len);
  write_be64(out.data() + 10, next_seq_);

  std::uint16_t msg_count = 0;
  std::size_t off = 0;
  while (off + 2 <= len && msg_count < max_messages_per_datagram) {
    const std::uint16_t msg_len = read_be16(data + off);
    if (msg_len == 0 || off + 2 + msg_len > len) {
      break;
    }
    const std::size_t needed = 20 + 2 + (out.size() - 20) + 2 + msg_len;
    if (needed > max_datagram_bytes) {
      break;
    }
    out.resize(out.size() + 2);
    write_be16(out.data() + out.size() - 2, msg_len);
    const std::size_t payload_off = out.size();
    out.resize(payload_off + msg_len);
    std::memcpy(out.data() + payload_off, data + off + 2, msg_len);
    off += 2 + msg_len;
    ++msg_count;
  }

  if (msg_count == 0) {
    return {};
  }
  write_be16(out.data() + 18, msg_count);
  next_seq_ += msg_count;
  consumed = off;
  return out;
}

}  // namespace mf::wire
