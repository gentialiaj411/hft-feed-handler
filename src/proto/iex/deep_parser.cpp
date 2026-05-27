#include "mf/proto/iex/deep_parser.hpp"

// IEX DEEP+ Specification v1.02 (downloaded from https://www.iex.io/documents/iex-deep-plus-specification)
// Trading Message Formats, pages 16-22:
// - Add Order 'a' (0x61)
// - Order Modify 'M' (0x4D)
// - Order Delete 'R' (0x52)
// - Order Executed 'L' (0x4C)
// - Trade 'T' (0x54)
// Administrative formats used here for sequencing/session awareness:
// - System Event 'S' (page 9)
// - Security Event 'E' (page 13)

#include <algorithm>
#include <cstring>

#include "mf/proto/iex/deep_messages.hpp"

namespace {

// Pass by value so call sites that read packed struct members
// (alignment 1 under #pragma pack) do not bind a misaligned reference.
// Binding a reference to a misaligned address is UB even on x86 where the
// hardware load itself succeeds; UBSAN -fsanitize=alignment correctly flags it.
template <typename T>
T read_le(T v) noexcept {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return v;
#else
  if constexpr (sizeof(T) == 2) return static_cast<T>(__builtin_bswap16(static_cast<std::uint16_t>(v)));
  if constexpr (sizeof(T) == 4) return static_cast<T>(__builtin_bswap32(static_cast<std::uint32_t>(v)));
  if constexpr (sizeof(T) == 8) return static_cast<T>(__builtin_bswap64(static_cast<std::uint64_t>(v)));
  return v;
#endif
}

mf::core::Side decode_side(char c) noexcept {
  if (c == '8') return mf::core::Side::Buy;
  if (c == '5') return mf::core::Side::Sell;
  return mf::core::Side::Unknown;
}

void copy_symbol(mf::core::SymbolKey& out, const std::array<char, 8>& src) noexcept {
  out.bytes = src;
}

std::uint32_t narrow_u64_to_u32(std::uint64_t v) noexcept {
  return static_cast<std::uint32_t>(std::min<std::uint64_t>(v, UINT32_MAX));
}

}  // namespace

namespace mf::proto::iex {

std::optional<mf::core::BookEvent> DeepParser::parse_message(
    std::span<const std::byte> payload,
    std::uint64_t sequence,
    std::uint64_t ingest_ts_ns,
    ParseStats& stats) const noexcept {
  if (payload.empty()) {
    ++stats.malformed_messages;
    return std::nullopt;
  }

  const char msg_type = static_cast<char>(payload[0]);
  ++stats.type_counts[static_cast<std::uint8_t>(msg_type)];
  ++stats.parsed_messages;

  mf::core::BookEvent ev{};
  ev.venue = mf::core::Venue::Iex;
  ev.sequence = sequence;
  ev.ingest_ts_ns = ingest_ts_ns;
  ev.raw_type = static_cast<std::uint8_t>(msg_type);

  auto require_exact = [&](std::size_t n) {
    if (payload.size() != n) {
      ++stats.malformed_messages;
      return false;
    }
    return true;
  };

  switch (msg_type) {
    case 'S': {
      if (!require_exact(sizeof(SystemEventMessage))) return std::nullopt;
      SystemEventMessage m{};
      std::memcpy(&m, payload.data(), sizeof(m));
      ev.type = mf::core::EventType::System;
      ev.exchange_ts_ns = read_le(m.timestamp_le);
      return ev;
    }
    case 'E': {
      if (!require_exact(sizeof(SecurityEventMessage))) return std::nullopt;
      SecurityEventMessage m{};
      std::memcpy(&m, payload.data(), sizeof(m));
      ev.type = mf::core::EventType::System;
      ev.exchange_ts_ns = read_le(m.timestamp_le);
      copy_symbol(ev.symbol, m.symbol);
      return ev;
    }
    case 'a': {
      if (!require_exact(sizeof(AddOrderMessage))) return std::nullopt;
      AddOrderMessage m{};
      std::memcpy(&m, payload.data(), sizeof(m));
      ev.type = mf::core::EventType::Add;
      ev.exchange_ts_ns = read_le(m.timestamp_le);
      copy_symbol(ev.symbol, m.symbol);
      ev.order_id = read_le(m.order_id_le);
      ev.qty = read_le(m.size_le);
      ev.price = narrow_u64_to_u32(read_le(m.price_le));
      ev.side = decode_side(m.side);
      return ev;
    }
    case 'M': {
      if (!require_exact(sizeof(ModifyOrderMessage))) return std::nullopt;
      ModifyOrderMessage m{};
      std::memcpy(&m, payload.data(), sizeof(m));
      ev.type = mf::core::EventType::Replace;
      ev.exchange_ts_ns = read_le(m.timestamp_le);
      copy_symbol(ev.symbol, m.symbol);
      ev.order_id = read_le(m.order_id_ref_le);
      ev.qty = read_le(m.size_le);
      ev.price = narrow_u64_to_u32(read_le(m.price_le));
      return ev;
    }
    case 'R': {
      if (!require_exact(sizeof(DeleteOrderMessage))) return std::nullopt;
      DeleteOrderMessage m{};
      std::memcpy(&m, payload.data(), sizeof(m));
      ev.type = mf::core::EventType::Delete;
      ev.exchange_ts_ns = read_le(m.timestamp_le);
      copy_symbol(ev.symbol, m.symbol);
      ev.order_id = read_le(m.order_id_ref_le);
      return ev;
    }
    case 'L': {
      if (!require_exact(sizeof(OrderExecutedMessage))) return std::nullopt;
      OrderExecutedMessage m{};
      std::memcpy(&m, payload.data(), sizeof(m));
      ev.type = mf::core::EventType::ExecutePrice;
      ev.exchange_ts_ns = read_le(m.timestamp_le);
      copy_symbol(ev.symbol, m.symbol);
      ev.order_id = read_le(m.order_id_ref_le);
      ev.qty = read_le(m.size_le);
      ev.price = narrow_u64_to_u32(read_le(m.price_le));
      ev.match_id = read_le(m.trade_id_le);
      return ev;
    }
    case 'T': {
      if (!require_exact(sizeof(TradeMessage))) return std::nullopt;
      TradeMessage m{};
      std::memcpy(&m, payload.data(), sizeof(m));
      ev.type = mf::core::EventType::Trade;
      ev.exchange_ts_ns = read_le(m.timestamp_le);
      copy_symbol(ev.symbol, m.symbol);
      ev.qty = read_le(m.size_le);
      ev.price = narrow_u64_to_u32(read_le(m.price_le));
      ev.match_id = read_le(m.trade_id_le);
      return ev;
    }
    // Admin/status messages we recognize but do not map into BookEvent state in v1.
    case 'D':
    case 'H':
    case 'I':
    case 'O':
    case 'P':
    case 'B':
    case 'C':
      ++stats.unimplemented_messages;
      ev.type = mf::core::EventType::Unknown;
      return ev;
    default:
      ++stats.unimplemented_messages;
      ev.type = mf::core::EventType::Unknown;
      return ev;
  }
}

}  // namespace mf::proto::iex
