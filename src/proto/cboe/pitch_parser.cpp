#include "mf/proto/cboe/pitch_parser.hpp"

#include <algorithm>
#include <cstring>

#include "mf/proto/cboe/pitch_messages.hpp"

namespace {

std::uint64_t parse_ascii_u64(const char* p, std::size_t n) noexcept {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const char c = p[i];
    if (c < '0' || c > '9') return 0;
    value = (value * 10ULL) + static_cast<std::uint64_t>(c - '0');
  }
  return value;
}

std::uint64_t parse_base36_u64(const char* p, std::size_t n) noexcept {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const char c = p[i];
    std::uint64_t digit = 0;
    if (c >= '0' && c <= '9') {
      digit = static_cast<std::uint64_t>(c - '0');
    } else if (c >= 'A' && c <= 'Z') {
      digit = static_cast<std::uint64_t>(10 + (c - 'A'));
    } else {
      return 0;
    }
    value = (value * 36ULL) + digit;
  }
  return value;
}

std::uint32_t parse_price_4dp(const char* p10) noexcept {
  // Price format: 6 whole + 4 decimal digits, no dot.
  const std::uint64_t v = parse_ascii_u64(p10, 10);
  return static_cast<std::uint32_t>(std::min<std::uint64_t>(v, UINT32_MAX));
}

std::uint32_t parse_long_price_6dp_to_4dp(const char* p14) noexcept {
  // Long Price format: 8 whole + 6 decimal digits. Normalize to 4dp by /100.
  const std::uint64_t v = parse_ascii_u64(p14, 14);
  const std::uint64_t normalized = v / 100ULL;
  return static_cast<std::uint32_t>(std::min<std::uint64_t>(normalized, UINT32_MAX));
}

mf::core::Side decode_side(char c) noexcept {
  if (c == 'B') return mf::core::Side::Buy;
  if (c == 'S') return mf::core::Side::Sell;
  return mf::core::Side::Unknown;
}

void copy_symbol(mf::core::SymbolKey& out, const char* src, std::size_t n) noexcept {
  out.bytes.fill(' ');
  const std::size_t m = (n < out.bytes.size()) ? n : out.bytes.size();
  std::memcpy(out.bytes.data(), src, m);
}

std::uint64_t ts_ms_to_ns(const char* ts8) noexcept {
  return parse_ascii_u64(ts8, 8) * 1000000ULL;
}

}  // namespace

namespace mf::proto::cboe {

std::optional<mf::core::BookEvent> PitchParser::parse_message(
    std::span<const std::byte> payload,
    std::uint64_t sequence,
    std::uint64_t ingest_ts_ns,
    ParseStats& stats) const noexcept {
  if (payload.size() < 9) {
    ++stats.malformed_messages;
    return std::nullopt;
  }

  const char msg_type = static_cast<char>(payload[8]);
  ++stats.type_counts[static_cast<std::uint8_t>(msg_type)];
  ++stats.parsed_messages;

  mf::core::BookEvent ev{};
  ev.venue = mf::core::Venue::Cboe;
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
    case 'A': {
      if (!require_exact(sizeof(AddOrderShortMessage))) return std::nullopt;
      AddOrderShortMessage m{};
      std::memcpy(&m, payload.data(), sizeof(m));
      ev.type = mf::core::EventType::Add;
      ev.exchange_ts_ns = ts_ms_to_ns(m.timestamp_ms.data());
      ev.order_id = parse_base36_u64(m.order_id.data(), m.order_id.size());
      ev.side = decode_side(m.side);
      ev.qty = static_cast<std::uint32_t>(parse_ascii_u64(m.shares.data(), m.shares.size()));
      copy_symbol(ev.symbol, m.symbol.data(), m.symbol.size());
      ev.price = parse_price_4dp(m.price.data());
      return ev;
    }
    case 'd': {
      if (!require_exact(sizeof(AddOrderLongMessage))) return std::nullopt;
      AddOrderLongMessage m{};
      std::memcpy(&m, payload.data(), sizeof(m));
      ev.type = mf::core::EventType::AddMpid;
      ev.exchange_ts_ns = ts_ms_to_ns(m.timestamp_ms.data());
      ev.order_id = parse_base36_u64(m.order_id.data(), m.order_id.size());
      ev.side = decode_side(m.side);
      ev.qty = static_cast<std::uint32_t>(parse_ascii_u64(m.shares.data(), m.shares.size()));
      copy_symbol(ev.symbol, m.symbol.data(), m.symbol.size());
      ev.price = parse_price_4dp(m.price.data());
      ev.mpid = std::array<char,4>{m.participant_id[0],m.participant_id[1],m.participant_id[2],m.participant_id[3]};
      return ev;
    }
    case '1': {
      if (!require_exact(sizeof(AddOrderExtendedMessage))) return std::nullopt;
      AddOrderExtendedMessage m{};
      std::memcpy(&m, payload.data(), sizeof(m));
      ev.type = mf::core::EventType::AddMpid;
      ev.exchange_ts_ns = ts_ms_to_ns(m.timestamp_ms.data());
      ev.order_id = parse_base36_u64(m.order_id.data(), m.order_id.size());
      ev.side = decode_side(m.side);
      ev.qty = static_cast<std::uint32_t>(parse_ascii_u64(m.shares.data(), m.shares.size()));
      copy_symbol(ev.symbol, m.symbol.data(), m.symbol.size());
      ev.price = parse_long_price_6dp_to_4dp(m.price_long.data());
      ev.mpid = std::array<char,4>{m.participant_id[0],m.participant_id[1],m.participant_id[2],m.participant_id[3]};
      return ev;
    }
    case 'E': {
      if (!require_exact(sizeof(OrderExecutedMessage))) return std::nullopt;
      OrderExecutedMessage m{};
      std::memcpy(&m, payload.data(), sizeof(m));
      ev.type = mf::core::EventType::Execute;
      ev.exchange_ts_ns = ts_ms_to_ns(m.timestamp_ms.data());
      ev.order_id = parse_base36_u64(m.order_id.data(), m.order_id.size());
      ev.qty = static_cast<std::uint32_t>(parse_ascii_u64(m.executed_shares.data(), m.executed_shares.size()));
      ev.match_id = parse_base36_u64(m.execution_id.data(), m.execution_id.size());
      return ev;
    }
    case 'X': {
      if (!require_exact(sizeof(OrderCancelMessage))) return std::nullopt;
      OrderCancelMessage m{};
      std::memcpy(&m, payload.data(), sizeof(m));
      ev.type = mf::core::EventType::Cancel;
      ev.exchange_ts_ns = ts_ms_to_ns(m.timestamp_ms.data());
      ev.order_id = parse_base36_u64(m.order_id.data(), m.order_id.size());
      ev.qty = static_cast<std::uint32_t>(parse_ascii_u64(m.canceled_shares.data(), m.canceled_shares.size()));
      return ev;
    }
    case 'P': {
      if (!require_exact(sizeof(TradeShortMessage))) return std::nullopt;
      TradeShortMessage m{};
      std::memcpy(&m, payload.data(), sizeof(m));
      ev.type = mf::core::EventType::Trade;
      ev.exchange_ts_ns = ts_ms_to_ns(m.timestamp_ms.data());
      ev.order_id = parse_base36_u64(m.order_id.data(), m.order_id.size());
      ev.side = decode_side(m.side);
      ev.qty = static_cast<std::uint32_t>(parse_ascii_u64(m.shares.data(), m.shares.size()));
      copy_symbol(ev.symbol, m.symbol.data(), m.symbol.size());
      ev.price = parse_price_4dp(m.price.data());
      ev.match_id = parse_base36_u64(m.execution_id.data(), m.execution_id.size());
      return ev;
    }
    case 'r': {
      if (!require_exact(sizeof(TradeLongMessage))) return std::nullopt;
      TradeLongMessage m{};
      std::memcpy(&m, payload.data(), sizeof(m));
      ev.type = mf::core::EventType::Trade;
      ev.exchange_ts_ns = ts_ms_to_ns(m.timestamp_ms.data());
      ev.order_id = parse_base36_u64(m.order_id.data(), m.order_id.size());
      ev.side = decode_side(m.side);
      ev.qty = static_cast<std::uint32_t>(parse_ascii_u64(m.shares.data(), m.shares.size()));
      copy_symbol(ev.symbol, m.symbol.data(), m.symbol.size());
      ev.price = parse_price_4dp(m.price.data());
      ev.match_id = parse_base36_u64(m.execution_id.data(), m.execution_id.size());
      return ev;
    }
    case '2': {
      if (!require_exact(sizeof(TradeExtendedMessage))) return std::nullopt;
      TradeExtendedMessage m{};
      std::memcpy(&m, payload.data(), sizeof(m));
      ev.type = mf::core::EventType::Trade;
      ev.exchange_ts_ns = ts_ms_to_ns(m.timestamp_ms.data());
      ev.order_id = parse_base36_u64(m.order_id.data(), m.order_id.size());
      ev.side = decode_side(m.side);
      ev.qty = static_cast<std::uint32_t>(parse_ascii_u64(m.shares.data(), m.shares.size()));
      copy_symbol(ev.symbol, m.symbol.data(), m.symbol.size());
      ev.price = parse_long_price_6dp_to_4dp(m.price_long.data());
      ev.match_id = parse_base36_u64(m.execution_id.data(), m.execution_id.size());
      return ev;
    }
    default:
      ev.type = mf::core::EventType::Unknown;
      return ev;
  }
}

}  // namespace mf::proto::cboe
