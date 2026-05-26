#include "mf/proto/mdp3/sbe_header.hpp"

#include <cstring>

namespace mf::proto::mdp3 {

namespace {

template <typename T>
T read_le(std::span<const std::byte> data, std::size_t off) noexcept {
  T v{};
  if (off + sizeof(T) <= data.size()) {
    std::memcpy(&v, data.data() + off, sizeof(T));
  }
  return v;
}

}  // namespace

bool decode_sbe_header(std::span<const std::byte> msg, SbeMessageHeader& out) noexcept {
  if (msg.size() < 10) {
    return false;
  }
  out.msg_size = read_le<std::uint16_t>(msg, 0);
  out.block_length = read_le<std::uint16_t>(msg, 2);
  out.template_id = read_le<std::uint16_t>(msg, 4);
  out.schema_id = read_le<std::uint16_t>(msg, 6);
  out.version = read_le<std::uint16_t>(msg, 8);
  if (out.msg_size < 10 || out.msg_size > msg.size()) {
    return false;
  }
  return true;
}

bool decode_mdp_packet_header(std::span<const std::byte> packet, MdpPacketHeader& out) noexcept {
  if (packet.size() < 12) {
    return false;
  }
  out.msg_seq_num = read_le<std::uint32_t>(packet, 0);
  out.sending_time = read_le<std::uint64_t>(packet, 4);
  return true;
}

}  // namespace mf::proto::mdp3
