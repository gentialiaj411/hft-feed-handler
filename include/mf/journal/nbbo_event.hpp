#pragma once

#include <array>
#include <bit>
#include <cstdint>

namespace mf::journal {

inline constexpr std::array<char, 4> kNbboJournalMagic = {'M', 'F', 'N', 'B'};
inline constexpr std::uint32_t kNbboJournalVersion = 1U;

struct NbboJournalHeader {
  std::array<char, 4> magic{};
  std::uint32_t version{0};
};

struct NbboJournalRecord {
  std::uint64_t monotonic_seq{0};
  std::uint64_t ingest_ts_ns{0};
  std::uint32_t payload_len{0};
  std::uint32_t crc32{0};
};

struct NbboEvent {
  std::uint64_t symbol_u64{0};
  std::uint64_t exchange_ts_ns{0};
  std::uint64_t ingest_ts_ns{0};
  bool has_bid{false};
  bool has_ask{false};
  std::uint32_t bid_price{0};
  std::uint32_t bid_qty{0};
  std::uint32_t ask_price{0};
  std::uint32_t ask_qty{0};
  std::uint8_t bid_venue{0};
  std::uint8_t ask_venue{0};
  std::uint64_t bid_venue_sequence{0};
  std::uint64_t ask_venue_sequence{0};
};

inline constexpr std::uint32_t kNbboPayloadLenV1 = static_cast<std::uint32_t>(sizeof(NbboEvent));

static_assert(std::endian::native == std::endian::little, "NBBO journal v1 requires little-endian host.");
static_assert(sizeof(NbboEvent) > 0, "NbboEvent size must be fixed.");

}  // namespace mf::journal
