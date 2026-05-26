#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/proto/mdp3/sbe_header.hpp"

namespace mf::proto::mdp3 {

struct ParseStats {
  std::array<std::uint64_t, 256> template_counts{};
  std::uint64_t parsed_messages{0};
  std::uint64_t malformed_messages{0};
  std::uint64_t book_events_emitted{0};
};

class Mdp3Parser {
 public:
  static constexpr std::uint16_t kTemplateMdIncrementalRefreshBook32 = 32;

  // Decode one MDP UDP payload (12-byte packet header + SBE messages).
  [[nodiscard]] std::vector<mf::core::BookEvent> parse_packet(
      std::span<const std::byte> udp_payload,
      std::uint64_t ingest_ts_ns,
      ParseStats& stats) const noexcept;

  // Decode template 32 body (after SBE message header).
  [[nodiscard]] std::vector<mf::core::BookEvent> parse_incremental_refresh_book32(
      std::span<const std::byte> body,
      std::uint64_t packet_seq,
      std::uint64_t transact_time_ns,
      std::uint64_t ingest_ts_ns,
      ParseStats& stats) const noexcept;

 private:
  [[nodiscard]] static mf::core::SymbolKey security_id_to_symbol(std::int32_t security_id) noexcept;
  [[nodiscard]] static std::uint32_t price9_mantissa_to_tick(std::int64_t mantissa) noexcept;
  [[nodiscard]] static mf::core::Side entry_type_to_side(char entry_type) noexcept;
  [[nodiscard]] static mf::core::EventType update_action_to_event_type(std::uint8_t action) noexcept;
};

}  // namespace mf::proto::mdp3
