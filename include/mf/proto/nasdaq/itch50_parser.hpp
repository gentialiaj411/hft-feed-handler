#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "mf/core/types.hpp"

namespace mf::proto::nasdaq {

struct ParseStats {
  std::array<std::uint64_t, 256> type_counts{};
  std::uint64_t parsed_messages{0};
  std::uint64_t malformed_messages{0};
};

class Itch50Parser {
 public:
  [[nodiscard]] std::optional<mf::core::BookEvent> parse_message(
      std::span<const std::byte> payload,
      std::uint64_t sequence,
      std::uint64_t ingest_ts_ns,
      ParseStats& stats) const noexcept;

  [[nodiscard]] std::optional<mf::core::BookEvent> parse_hot_message_simd(
      std::span<const std::byte> payload,
      std::uint64_t sequence,
      std::uint64_t ingest_ts_ns,
      ParseStats& stats) const noexcept;

  [[nodiscard]] static bool simd_hot_path_available() noexcept;

 private:
  [[nodiscard]] static std::uint64_t decode_ts6(const std::uint8_t ts[6]) noexcept;
  [[nodiscard]] static mf::core::Side decode_side(char c) noexcept;
};

}  // namespace mf::proto::nasdaq
