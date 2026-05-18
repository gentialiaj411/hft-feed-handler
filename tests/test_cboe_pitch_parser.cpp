#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/proto/cboe/pitch_messages.hpp"
#include "mf/proto/cboe/pitch_parser.hpp"

namespace {

template <typename Msg>
std::vector<std::byte> pack_message(const Msg& msg) {
  std::vector<std::byte> out(sizeof(Msg));
  std::memcpy(out.data(), &msg, sizeof(Msg));
  return out;
}

template <std::size_t N>
void fill_ascii(std::array<char, N>& dst, const char* src) {
  for (std::size_t i = 0; i < N; ++i) {
    dst[i] = src[i];
  }
}

mf::proto::cboe::AddOrderShortMessage make_add_order_short() {
  mf::proto::cboe::AddOrderShortMessage m{};
  fill_ascii(m.timestamp_ms, "00123456");
  m.msg_type = 'A';
  fill_ascii(m.order_id, "00000000000A");
  m.side = 'B';
  fill_ascii(m.shares, "000100");
  fill_ascii(m.symbol, "AAPL  ");
  fill_ascii(m.price, "0000010000");
  m.reserved = ' ';
  return m;
}

mf::proto::cboe::OrderExecutedMessage make_order_executed() {
  mf::proto::cboe::OrderExecutedMessage m{};
  fill_ascii(m.timestamp_ms, "00123457");
  m.msg_type = 'E';
  fill_ascii(m.order_id, "00000000000B");
  fill_ascii(m.executed_shares, "000050");
  fill_ascii(m.execution_id, "00000000000C");
  return m;
}

mf::proto::cboe::OrderCancelMessage make_order_cancel() {
  mf::proto::cboe::OrderCancelMessage m{};
  fill_ascii(m.timestamp_ms, "00123458");
  m.msg_type = 'X';
  fill_ascii(m.order_id, "00000000000D");
  fill_ascii(m.canceled_shares, "000025");
  return m;
}

mf::proto::cboe::TradeShortMessage make_trade_short() {
  mf::proto::cboe::TradeShortMessage m{};
  fill_ascii(m.timestamp_ms, "00123459");
  m.msg_type = 'P';
  fill_ascii(m.order_id, "00000000000E");
  m.side = 'S';
  fill_ascii(m.shares, "000200");
  fill_ascii(m.symbol, "MSFT  ");
  fill_ascii(m.price, "0000025000");
  fill_ascii(m.execution_id, "00000000000F");
  return m;
}

void test_add_order_short() {
  mf::proto::cboe::PitchParser parser{};
  mf::proto::cboe::ParseStats stats{};
  const auto payload = pack_message(make_add_order_short());
  const auto ev = parser.parse_message(std::span<const std::byte>(payload.data(), payload.size()), 7, 11, stats);
  assert(ev.has_value());
  assert(ev->type == mf::core::EventType::Add);
  assert(ev->side == mf::core::Side::Buy);
  assert(ev->order_id == 10ULL);
  assert(ev->qty == 100U);
  assert(ev->price == 10000U);
  assert(ev->sequence == 7ULL);
  assert(ev->ingest_ts_ns == 11ULL);
  assert(stats.parsed_messages == 1ULL);
  assert(stats.malformed_messages == 0ULL);
}

void test_order_executed() {
  mf::proto::cboe::PitchParser parser{};
  mf::proto::cboe::ParseStats stats{};
  const auto payload = pack_message(make_order_executed());
  const auto ev = parser.parse_message(std::span<const std::byte>(payload.data(), payload.size()), 8, 12, stats);
  assert(ev.has_value());
  assert(ev->type == mf::core::EventType::Execute);
  assert(ev->order_id == 11ULL);
  assert(ev->qty == 50U);
  assert(ev->match_id == 12ULL);
}

void test_order_cancel() {
  mf::proto::cboe::PitchParser parser{};
  mf::proto::cboe::ParseStats stats{};
  const auto payload = pack_message(make_order_cancel());
  const auto ev = parser.parse_message(std::span<const std::byte>(payload.data(), payload.size()), 9, 13, stats);
  assert(ev.has_value());
  assert(ev->type == mf::core::EventType::Cancel);
  assert(ev->order_id == 13ULL);
  assert(ev->qty == 25U);
}

void test_trade_short() {
  mf::proto::cboe::PitchParser parser{};
  mf::proto::cboe::ParseStats stats{};
  const auto payload = pack_message(make_trade_short());
  const auto ev = parser.parse_message(std::span<const std::byte>(payload.data(), payload.size()), 10, 14, stats);
  assert(ev.has_value());
  assert(ev->type == mf::core::EventType::Trade);
  assert(ev->side == mf::core::Side::Sell);
  assert(ev->order_id == 14ULL);
  assert(ev->qty == 200U);
  assert(ev->price == 25000U);
}

}  // namespace

int main() {
  test_add_order_short();
  test_order_executed();
  test_order_cancel();
  test_trade_short();
  return 0;
}
