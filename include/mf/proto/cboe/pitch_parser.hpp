#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "mf/core/types.hpp"

namespace mf::proto::cboe {

struct ParseStats {
  std::array<std::uint64_t, 256> type_counts{};
  std::uint64_t parsed_messages{0};
  std::uint64_t malformed_messages{0};
};

class PitchParser {
 public:
  [[nodiscard]] std::optional<mf::core::BookEvent> parse_message(
      std::span<const std::byte> payload,
      std::uint64_t sequence,
      std::uint64_t ingest_ts_ns,
      ParseStats& stats) const noexcept;
};

}  // namespace mf::proto::cboe
