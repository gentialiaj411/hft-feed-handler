#include "mf/proto/nasdaq/itch50_parser.hpp"

#include <cstring>

#include "mf/proto/nasdaq/itch50_messages.hpp"
#include "mf/proto/read_be.hpp"

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
#include <immintrin.h>
#endif

namespace mf::proto::nasdaq {

namespace {

std::uint32_t bswap32(std::uint32_t x) noexcept {
#if defined(_MSC_VER)
  return _byteswap_ulong(x);
#elif defined(__GNUC__) || defined(__clang__)
  return __builtin_bswap32(x);
#else
  return ((x & 0x000000ffU) << 24U) | ((x & 0x0000ff00U) << 8U) | ((x & 0x00ff0000U) >> 8U) | ((x & 0xff000000U) >> 24U);
#endif
}

std::uint64_t bswap64(std::uint64_t x) noexcept {
#if defined(_MSC_VER)
  return _byteswap_uint64(x);
#elif defined(__GNUC__) || defined(__clang__)
  return __builtin_bswap64(x);
#else
  return (static_cast<std::uint64_t>(bswap32(static_cast<std::uint32_t>(x))) << 32U) |
         bswap32(static_cast<std::uint32_t>(x >> 32U));
#endif
}

std::uint64_t read_u64_fast(const std::byte* p) noexcept {
  std::uint64_t v = 0;
  std::memcpy(&v, p, sizeof(v));
  return bswap64(v);
}

std::uint32_t read_u32_fast(const std::byte* p) noexcept {
  std::uint32_t v = 0;
  std::memcpy(&v, p, sizeof(v));
  return bswap32(v);
}

std::uint64_t read_ts6_fast(const std::byte* p) noexcept {
  return (static_cast<std::uint64_t>(static_cast<std::uint8_t>(p[0])) << 40U) |
         (static_cast<std::uint64_t>(static_cast<std::uint8_t>(p[1])) << 32U) |
         (static_cast<std::uint64_t>(static_cast<std::uint8_t>(p[2])) << 24U) |
         (static_cast<std::uint64_t>(static_cast<std::uint8_t>(p[3])) << 16U) |
         (static_cast<std::uint64_t>(static_cast<std::uint8_t>(p[4])) << 8U) |
         static_cast<std::uint64_t>(static_cast<std::uint8_t>(p[5]));
}

void copy_symbol_fast(const std::byte* src, std::array<char, 8>& dst) noexcept {
  std::memcpy(dst.data(), src, dst.size());
}

}  // namespace

std::uint64_t Itch50Parser::decode_ts6(const std::uint8_t ts[6]) noexcept {
  return (static_cast<std::uint64_t>(ts[0]) << 40U) |
         (static_cast<std::uint64_t>(ts[1]) << 32U) |
         (static_cast<std::uint64_t>(ts[2]) << 24U) |
         (static_cast<std::uint64_t>(ts[3]) << 16U) |
         (static_cast<std::uint64_t>(ts[4]) << 8U) |
         static_cast<std::uint64_t>(ts[5]);
}

mf::core::Side Itch50Parser::decode_side(char c) noexcept {
  if (c == 'B') return mf::core::Side::Buy;
  if (c == 'S') return mf::core::Side::Sell;
  return mf::core::Side::Unknown;
}

bool Itch50Parser::simd_hot_path_available() noexcept {
#if defined(__AVX2__) || defined(_M_AVX2)
  return true;
#else
  return false;
#endif
}

std::optional<mf::core::BookEvent> Itch50Parser::parse_hot_message_simd(
    std::span<const std::byte> payload,
    std::uint64_t sequence,
    std::uint64_t ingest_ts_ns,
    ParseStats& stats) const noexcept {
  if (payload.empty()) {
    ++stats.malformed_messages;
    return std::nullopt;
  }

  const char type = static_cast<char>(payload[0]);
  if (type != 'A' && type != 'E' && type != 'X' && type != 'D') {
    return parse_message(payload, sequence, ingest_ts_ns, stats);
  }

  ++stats.type_counts[static_cast<std::uint8_t>(type)];
  ++stats.parsed_messages;

  auto require_size = [&](std::size_t n) {
    if (payload.size() < n) {
      ++stats.malformed_messages;
      return false;
    }
    return true;
  };

  mf::core::BookEvent ev{};
  ev.venue = mf::core::Venue::Nasdaq;
  ev.sequence = sequence;
  ev.ingest_ts_ns = ingest_ts_ns;
  ev.raw_type = static_cast<std::uint8_t>(type);

  const auto* p = payload.data() + 1;
  switch (type) {
    case 'A': {
      if (!require_size(sizeof(AddOrderMessage) + 1)) return std::nullopt;
#if defined(__AVX2__) || defined(_M_AVX2)
      alignas(32) std::byte block[32]{};
      std::memcpy(block, p + 10, 25);
      (void)_mm256_load_si256(reinterpret_cast<const __m256i*>(block));
#endif
      ev.type = mf::core::EventType::Add;
      ev.exchange_ts_ns = read_ts6_fast(p + 4);
      ev.order_id = read_u64_fast(p + 10);
      ev.side = decode_side(static_cast<char>(p[18]));
      ev.qty = read_u32_fast(p + 19);
      copy_symbol_fast(p + 23, ev.symbol.bytes);
      ev.price = read_u32_fast(p + 31);
      return ev;
    }
    case 'E': {
      if (!require_size(sizeof(OrderExecutedMessage) + 1)) return std::nullopt;
      ev.type = mf::core::EventType::Execute;
      ev.exchange_ts_ns = read_ts6_fast(p + 4);
      ev.order_id = read_u64_fast(p + 10);
      ev.qty = read_u32_fast(p + 18);
      ev.match_id = read_u64_fast(p + 22);
      return ev;
    }
    case 'X': {
      if (!require_size(sizeof(OrderCancelMessage) + 1)) return std::nullopt;
      ev.type = mf::core::EventType::Cancel;
      ev.exchange_ts_ns = read_ts6_fast(p + 4);
      ev.order_id = read_u64_fast(p + 10);
      ev.qty = read_u32_fast(p + 18);
      return ev;
    }
    case 'D': {
      if (!require_size(sizeof(OrderDeleteMessage) + 1)) return std::nullopt;
      ev.type = mf::core::EventType::Delete;
      ev.exchange_ts_ns = read_ts6_fast(p + 4);
      ev.order_id = read_u64_fast(p + 10);
      return ev;
    }
    default:
      break;
  }
  return parse_message(payload, sequence, ingest_ts_ns, stats);
}

std::optional<mf::core::BookEvent> Itch50Parser::parse_message(
    std::span<const std::byte> payload,
    std::uint64_t sequence,
    std::uint64_t ingest_ts_ns,
    ParseStats& stats) const noexcept {
  if (payload.empty()) {
    ++stats.malformed_messages;
    return std::nullopt;
  }

  const char type = static_cast<char>(payload[0]);
  ++stats.type_counts[static_cast<std::uint8_t>(type)];
  ++stats.parsed_messages;

  mf::core::BookEvent ev{};
  ev.venue = mf::core::Venue::Nasdaq;
  ev.sequence = sequence;
  ev.ingest_ts_ns = ingest_ts_ns;
  ev.raw_type = static_cast<std::uint8_t>(type);

  auto require_size = [&](std::size_t n) {
    if (payload.size() < n) {
      ++stats.malformed_messages;
      return false;
    }
    return true;
  };

  switch (type) {
    case 'A': {
      if (!require_size(sizeof(AddOrderMessage) + 1)) return std::nullopt;
      AddOrderMessage m{};
      std::memcpy(&m, payload.data() + 1, sizeof(m));
      ev.type = mf::core::EventType::Add;
      ev.exchange_ts_ns = decode_ts6(m.timestamp);
      ev.order_id = read_be<std::uint64_t>(reinterpret_cast<const std::byte*>(&m.order_ref_be));
      ev.qty = read_be<std::uint32_t>(reinterpret_cast<const std::byte*>(&m.shares_be));
      ev.price = read_be<std::uint32_t>(reinterpret_cast<const std::byte*>(&m.price_be));
      ev.side = decode_side(m.buy_sell);
      ev.symbol.bytes = m.stock;
      return ev;
    }
    case 'F': {
      if (!require_size(sizeof(AddOrderMpidMessage) + 1)) return std::nullopt;
      AddOrderMpidMessage m{};
      std::memcpy(&m, payload.data() + 1, sizeof(m));
      ev.type = mf::core::EventType::AddMpid;
      ev.exchange_ts_ns = decode_ts6(m.base.timestamp);
      ev.order_id = read_be<std::uint64_t>(reinterpret_cast<const std::byte*>(&m.base.order_ref_be));
      ev.qty = read_be<std::uint32_t>(reinterpret_cast<const std::byte*>(&m.base.shares_be));
      ev.price = read_be<std::uint32_t>(reinterpret_cast<const std::byte*>(&m.base.price_be));
      ev.side = decode_side(m.base.buy_sell);
      ev.symbol.bytes = m.base.stock;
      ev.mpid = m.attribution;
      return ev;
    }
    case 'E': {
      if (!require_size(sizeof(OrderExecutedMessage) + 1)) return std::nullopt;
      OrderExecutedMessage m{};
      std::memcpy(&m, payload.data() + 1, sizeof(m));
      ev.type = mf::core::EventType::Execute;
      ev.exchange_ts_ns = decode_ts6(m.timestamp);
      ev.order_id = read_be<std::uint64_t>(reinterpret_cast<const std::byte*>(&m.order_ref_be));
      ev.qty = read_be<std::uint32_t>(reinterpret_cast<const std::byte*>(&m.executed_shares_be));
      ev.match_id = read_be<std::uint64_t>(reinterpret_cast<const std::byte*>(&m.match_number_be));
      return ev;
    }
    case 'C': {
      if (!require_size(sizeof(OrderExecutedPriceMessage) + 1)) return std::nullopt;
      OrderExecutedPriceMessage m{};
      std::memcpy(&m, payload.data() + 1, sizeof(m));
      ev.type = mf::core::EventType::ExecutePrice;
      ev.exchange_ts_ns = decode_ts6(m.timestamp);
      ev.order_id = read_be<std::uint64_t>(reinterpret_cast<const std::byte*>(&m.order_ref_be));
      ev.qty = read_be<std::uint32_t>(reinterpret_cast<const std::byte*>(&m.executed_shares_be));
      ev.match_id = read_be<std::uint64_t>(reinterpret_cast<const std::byte*>(&m.match_number_be));
      ev.price = read_be<std::uint32_t>(reinterpret_cast<const std::byte*>(&m.execution_price_be));
      return ev;
    }
    case 'X': {
      if (!require_size(sizeof(OrderCancelMessage) + 1)) return std::nullopt;
      OrderCancelMessage m{};
      std::memcpy(&m, payload.data() + 1, sizeof(m));
      ev.type = mf::core::EventType::Cancel;
      ev.exchange_ts_ns = decode_ts6(m.timestamp);
      ev.order_id = read_be<std::uint64_t>(reinterpret_cast<const std::byte*>(&m.order_ref_be));
      ev.qty = read_be<std::uint32_t>(reinterpret_cast<const std::byte*>(&m.canceled_shares_be));
      return ev;
    }
    case 'D': {
      if (!require_size(sizeof(OrderDeleteMessage) + 1)) return std::nullopt;
      OrderDeleteMessage m{};
      std::memcpy(&m, payload.data() + 1, sizeof(m));
      ev.type = mf::core::EventType::Delete;
      ev.exchange_ts_ns = decode_ts6(m.timestamp);
      ev.order_id = read_be<std::uint64_t>(reinterpret_cast<const std::byte*>(&m.order_ref_be));
      return ev;
    }
    case 'U': {
      if (!require_size(sizeof(OrderReplaceMessage) + 1)) return std::nullopt;
      OrderReplaceMessage m{};
      std::memcpy(&m, payload.data() + 1, sizeof(m));
      ev.type = mf::core::EventType::Replace;
      ev.exchange_ts_ns = decode_ts6(m.timestamp);
      ev.reference_order_id = read_be<std::uint64_t>(reinterpret_cast<const std::byte*>(&m.original_order_ref_be));
      ev.order_id = read_be<std::uint64_t>(reinterpret_cast<const std::byte*>(&m.new_order_ref_be));
      ev.qty = read_be<std::uint32_t>(reinterpret_cast<const std::byte*>(&m.shares_be));
      ev.price = read_be<std::uint32_t>(reinterpret_cast<const std::byte*>(&m.price_be));
      return ev;
    }
    case 'P': {
      if (!require_size(sizeof(TradeMessage) + 1)) return std::nullopt;
      TradeMessage m{};
      std::memcpy(&m, payload.data() + 1, sizeof(m));
      ev.type = mf::core::EventType::Trade;
      ev.exchange_ts_ns = decode_ts6(m.timestamp);
      ev.order_id = read_be<std::uint64_t>(reinterpret_cast<const std::byte*>(&m.order_ref_be));
      ev.side = decode_side(m.buy_sell);
      ev.qty = read_be<std::uint32_t>(reinterpret_cast<const std::byte*>(&m.shares_be));
      ev.symbol.bytes = m.stock;
      ev.price = read_be<std::uint32_t>(reinterpret_cast<const std::byte*>(&m.price_be));
      ev.match_id = read_be<std::uint64_t>(reinterpret_cast<const std::byte*>(&m.match_number_be));
      return ev;
    }
    case 'Q': {
      if (!require_size(sizeof(CrossTradeMessage) + 1)) return std::nullopt;
      CrossTradeMessage m{};
      std::memcpy(&m, payload.data() + 1, sizeof(m));
      ev.type = mf::core::EventType::CrossTrade;
      ev.exchange_ts_ns = decode_ts6(m.timestamp);
      ev.qty = static_cast<std::uint32_t>(read_be<std::uint64_t>(reinterpret_cast<const std::byte*>(&m.shares_be)));
      ev.symbol.bytes = m.stock;
      ev.price = read_be<std::uint32_t>(reinterpret_cast<const std::byte*>(&m.cross_price_be));
      ev.match_id = read_be<std::uint64_t>(reinterpret_cast<const std::byte*>(&m.match_number_be));
      return ev;
    }
    case 'I': {
      if (!require_size(sizeof(NoiiMessage) + 1)) return std::nullopt;
      NoiiMessage m{};
      std::memcpy(&m, payload.data() + 1, sizeof(m));
      ev.type = mf::core::EventType::Imbalance;
      ev.exchange_ts_ns = decode_ts6(m.timestamp);
      ev.qty = static_cast<std::uint32_t>(read_be<std::uint64_t>(reinterpret_cast<const std::byte*>(&m.imbalance_shares_be)));
      ev.symbol.bytes = m.stock;
      return ev;
    }
    case 'S': {
      if (!require_size(sizeof(SystemEventMessage) + 1)) return std::nullopt;
      SystemEventMessage m{};
      std::memcpy(&m, payload.data() + 1, sizeof(m));
      ev.type = mf::core::EventType::System;
      ev.exchange_ts_ns = decode_ts6(m.timestamp);
      return ev;
    }
    case 'R': {
      if (!require_size(sizeof(StockDirectoryMessage) + 1)) return std::nullopt;
      StockDirectoryMessage m{};
      std::memcpy(&m, payload.data() + 1, sizeof(m));
      ev.type = mf::core::EventType::StockDirectory;
      ev.exchange_ts_ns = decode_ts6(m.timestamp);
      ev.symbol.bytes = m.stock;
      return ev;
    }
    default:
      ev.type = mf::core::EventType::Unknown;
      return ev;
  }
}

}  // namespace mf::proto::nasdaq
