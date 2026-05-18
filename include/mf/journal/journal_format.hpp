#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

#include "mf/core/types.hpp"

namespace mf::journal {

// v1 stores fixed-size BookEvent records; future versions may add variable-length envelopes.
inline constexpr std::array<char, 4> kJournalMagic = {'M', 'F', 'J', 'N'};
inline constexpr std::uint32_t kJournalVersion = 1U;

struct JournalHeader {
  std::array<char, 4> magic{};
  std::uint32_t version{0};
};

struct JournalRecord {
  std::uint64_t monotonic_seq{0};
  std::uint64_t ingest_ts_ns{0};
  std::uint32_t payload_len{0};
  std::uint32_t crc32{0};
};

inline constexpr std::uint32_t kJournalPayloadLenV1 = static_cast<std::uint32_t>(sizeof(mf::core::BookEvent));

static_assert(std::endian::native == std::endian::little, "Journal v1 requires little-endian host.");
static_assert(sizeof(mf::core::BookEvent) > 0, "BookEvent size must be fixed.");

}  // namespace mf::journal
