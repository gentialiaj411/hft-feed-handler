#include "mf/proto/mdp3/mdp3_parser.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace mf::proto::mdp3 {

namespace {

constexpr std::int64_t kPrice9NullMantissa = std::numeric_limits<std::int64_t>::min();
constexpr std::int32_t kInt32Null = std::numeric_limits<std::int32_t>::min();

template <typename T>
T read_le(std::span<const std::byte> data, std::size_t off) noexcept {
  T v{};
  if (off + sizeof(T) <= data.size()) {
    std::memcpy(&v, data.data() + off, sizeof(T));
  }
  return v;
}

}  // namespace

mf::core::SymbolKey Mdp3Parser::security_id_to_symbol(std::int32_t security_id) noexcept {
  mf::core::SymbolKey sym{};
  sym.bytes.fill(' ');
  if (security_id == kInt32Null) {
    return sym;
  }
  char tmp[16];
  const int n = std::snprintf(tmp, sizeof(tmp), "%d", security_id);
  const std::size_t m = static_cast<std::size_t>(std::max(0, std::min(n, static_cast<int>(sym.bytes.size()))));
  std::memcpy(sym.bytes.data(), tmp, m);
  return sym;
}

std::uint32_t Mdp3Parser::price9_mantissa_to_tick(std::int64_t mantissa) noexcept {
  if (mantissa == kPrice9NullMantissa) {
    return 0;
  }
  // PRICE9 exponent is fixed at -9; canonical price uses 1/10000 dollar ticks like ITCH.
  const std::int64_t ticks = mantissa / 100000;
  if (ticks <= 0) {
    return 0;
  }
  return static_cast<std::uint32_t>(std::min<std::int64_t>(ticks, UINT32_MAX));
}

mf::core::Side Mdp3Parser::entry_type_to_side(char entry_type) noexcept {
  if (entry_type == '0') return mf::core::Side::Buy;
  if (entry_type == '1') return mf::core::Side::Sell;
  return mf::core::Side::Unknown;
}

mf::core::EventType Mdp3Parser::update_action_to_event_type(std::uint8_t action) noexcept {
  switch (action) {
    case 0:
      return mf::core::EventType::Add;
    case 1:
      return mf::core::EventType::Replace;
    case 2:
    case 3:
    case 4:
      return mf::core::EventType::Delete;
    default:
      return mf::core::EventType::Unknown;
  }
}

std::vector<mf::core::BookEvent> Mdp3Parser::parse_incremental_refresh_book32(
    std::span<const std::byte> body,
    std::uint64_t packet_seq,
    std::uint64_t transact_time_ns,
    std::uint64_t ingest_ts_ns,
    ParseStats& stats) const noexcept {
  std::vector<mf::core::BookEvent> out;
  if (body.size() < 11) {
    ++stats.malformed_messages;
    return out;
  }

  const std::uint64_t transact_time = read_le<std::uint64_t>(body, 0);
  const std::uint64_t exchange_ts = transact_time != 0 ? transact_time : transact_time_ns;

  std::size_t pos = 11;
  if (pos + 3 > body.size()) {
    ++stats.malformed_messages;
    return out;
  }
  const std::uint16_t entry_block_len = read_le<std::uint16_t>(body, pos);
  const std::uint8_t entry_count = static_cast<std::uint8_t>(body[pos + 2]);
  pos += 3;
  if (entry_block_len < 32 || pos + static_cast<std::size_t>(entry_count) * entry_block_len > body.size()) {
    ++stats.malformed_messages;
    return out;
  }

  for (std::uint8_t i = 0; i < entry_count; ++i) {
    const std::size_t eoff = pos + static_cast<std::size_t>(i) * entry_block_len;
    if (eoff + 32 > body.size()) {
      ++stats.malformed_messages;
      break;
    }
    const std::int64_t px_mantissa = read_le<std::int64_t>(body, eoff + 0);
    const std::int32_t entry_size = read_le<std::int32_t>(body, eoff + 8);
    const std::int32_t security_id = read_le<std::int32_t>(body, eoff + 12);
    const std::uint32_t rpt_seq = read_le<std::uint32_t>(body, eoff + 16);
    const std::uint8_t update_action = static_cast<std::uint8_t>(body[eoff + 25]);
    const char entry_type = static_cast<char>(body[eoff + 26]);

    if (entry_type == 'J') {
      continue;
    }

    mf::core::BookEvent ev{};
    ev.venue = mf::core::Venue::Cme;
    ev.type = update_action_to_event_type(update_action);
    ev.sequence = (rpt_seq != 0) ? static_cast<std::uint64_t>(rpt_seq)
                                 : (packet_seq << 32U) | static_cast<std::uint64_t>(i);
    ev.exchange_ts_ns = exchange_ts;
    ev.ingest_ts_ns = ingest_ts_ns;
    ev.symbol = security_id_to_symbol(security_id);
    ev.order_id = ev.sequence;
    ev.qty = (entry_size > 0) ? static_cast<std::uint32_t>(std::min<std::int32_t>(entry_size, INT32_MAX)) : 0;
    ev.price = price9_mantissa_to_tick(px_mantissa);
    ev.side = entry_type_to_side(entry_type);
    ev.raw_type = static_cast<std::uint8_t>(kTemplateMdIncrementalRefreshBook32);
    out.push_back(ev);
    ++stats.book_events_emitted;
  }
  return out;
}

std::vector<mf::core::BookEvent> Mdp3Parser::parse_packet(
    std::span<const std::byte> udp_payload,
    std::uint64_t ingest_ts_ns,
    ParseStats& stats) const noexcept {
  std::vector<mf::core::BookEvent> out;
  MdpPacketHeader pkt_hdr {};
  if (!decode_mdp_packet_header(udp_payload, pkt_hdr)) {
    ++stats.malformed_messages;
    return out;
  }

  std::size_t pos = 12;
  while (pos + 10 <= udp_payload.size()) {
    const auto msg = udp_payload.subspan(pos);
    SbeMessageHeader hdr {};
    if (!decode_sbe_header(msg, hdr)) {
      ++stats.malformed_messages;
      break;
    }
    ++stats.template_counts[hdr.template_id % 256];
    ++stats.parsed_messages;

    const auto body = msg.subspan(10, hdr.msg_size - 10);
    if (hdr.template_id == kTemplateMdIncrementalRefreshBook32) {
      auto events = parse_incremental_refresh_book32(
          body, pkt_hdr.msg_seq_num, pkt_hdr.sending_time, ingest_ts_ns, stats);
      out.insert(out.end(), events.begin(), events.end());
    }
    pos += hdr.msg_size;
  }
  return out;
}

}  // namespace mf::proto::mdp3
