#include "mf/proto/nyse/pillar_parser.hpp"

#include <cstring>

#include "mf/proto/nyse/pillar_messages.hpp"

namespace {

template <typename T>
T read_le(const T& v) noexcept {
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
  if (c == 'B') return mf::core::Side::Buy;
  if (c == 'S') return mf::core::Side::Sell;
  return mf::core::Side::Unknown;
}

}  // namespace

namespace mf::proto::nyse {

std::optional<mf::core::BookEvent> PillarParser::parse_message(
    std::span<const std::byte> payload,
    std::uint64_t sequence,
    std::uint64_t ingest_ts_ns,
    ParseStats& stats) const noexcept {
  if (payload.size() < sizeof(CommonHeader)) {
    ++stats.malformed_messages;
    return std::nullopt;
  }

  CommonHeader h{};
  std::memcpy(&h, payload.data(), sizeof(h));
  const std::uint16_t msg_type = read_le(h.msg_type_le);
  ++stats.type_counts[msg_type];
  ++stats.parsed_messages;

  mf::core::BookEvent ev{};
  ev.venue = mf::core::Venue::Nyse;
  ev.sequence = sequence;
  ev.ingest_ts_ns = ingest_ts_ns;
  ev.exchange_ts_ns = read_le(h.source_time_ns_le);
  ev.raw_type = static_cast<std::uint8_t>(msg_type & 0xFFU);

  auto require_size = [&](std::size_t n) {
    if (payload.size() < n) {
      ++stats.malformed_messages;
      return false;
    }
    return true;
  };

  // Scaffold mapping for message families; exact on-wire type ids finalized once
  // official Pillar Integrated spec tables are pinned in repo.
  switch (msg_type) {
    case 0x1001: {
      if (!require_size(sizeof(AddOrderMessage))) return std::nullopt;
      AddOrderMessage m{};
      std::memcpy(&m, payload.data(), sizeof(m));
      ev.type = mf::core::EventType::Add;
      ev.order_id = read_le(m.order_id_le);
      ev.price = read_le(m.price_le);
      ev.qty = read_le(m.qty_le);
      ev.side = decode_side(m.side);
      ev.symbol.bytes = m.symbol;
      return ev;
    }
    case 0x1002: {
      if (!require_size(sizeof(ModifyOrderMessage))) return std::nullopt;
      ModifyOrderMessage m{};
      std::memcpy(&m, payload.data(), sizeof(m));
      ev.type = mf::core::EventType::Replace;
      ev.order_id = read_le(m.order_id_le);
      ev.price = read_le(m.new_price_le);
      ev.qty = read_le(m.new_qty_le);
      return ev;
    }
    case 0x1003: {
      if (!require_size(sizeof(DeleteOrderMessage))) return std::nullopt;
      DeleteOrderMessage m{};
      std::memcpy(&m, payload.data(), sizeof(m));
      ev.type = mf::core::EventType::Delete;
      ev.order_id = read_le(m.order_id_le);
      return ev;
    }
    case 0x1004: {
      if (!require_size(sizeof(ExecutionMessage))) return std::nullopt;
      ExecutionMessage m{};
      std::memcpy(&m, payload.data(), sizeof(m));
      ev.type = mf::core::EventType::Execute;
      ev.order_id = read_le(m.order_id_le);
      ev.qty = read_le(m.executed_qty_le);
      ev.match_id = read_le(m.match_id_le);
      return ev;
    }
    case 0x1005: {
      if (!require_size(sizeof(TradeMessage))) return std::nullopt;
      TradeMessage m{};
      std::memcpy(&m, payload.data(), sizeof(m));
      ev.type = mf::core::EventType::Trade;
      ev.match_id = read_le(m.trade_id_le);
      ev.price = read_le(m.price_le);
      ev.qty = read_le(m.qty_le);
      ev.side = decode_side(m.side);
      ev.symbol.bytes = m.symbol;
      return ev;
    }
    default:
      ev.type = mf::core::EventType::Unknown;
      return ev;
  }
}

}  // namespace mf::proto::nyse
