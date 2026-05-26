#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace mf::proto::mdp3 {

struct SbeMessageHeader {
  std::uint16_t msg_size{0};
  std::uint16_t block_length{0};
  std::uint16_t template_id{0};
  std::uint16_t schema_id{0};
  std::uint16_t version{0};
};

struct MdpPacketHeader {
  std::uint32_t msg_seq_num{0};
  std::uint64_t sending_time{0};
};

[[nodiscard]] bool decode_sbe_header(std::span<const std::byte> msg, SbeMessageHeader& out) noexcept;

[[nodiscard]] bool decode_mdp_packet_header(std::span<const std::byte> packet, MdpPacketHeader& out) noexcept;

}  // namespace mf::proto::mdp3
