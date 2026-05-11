#pragma once

#include <array>
#include <cstdint>

namespace mf::proto::cboe {

// Cboe Titanium U.S. Equities TCP PITCH uses fixed-length ASCII messages.
// Offsets/lengths follow the exchange specification.
#pragma pack(push, 1)

struct AddOrderShortMessage {
  std::array<char, 8> timestamp_ms;
  char msg_type;                 // 'A'
  std::array<char, 12> order_id; // Base36
  char side;                     // B/S
  std::array<char, 6> shares;
  std::array<char, 6> symbol;
  std::array<char, 10> price;
  char reserved;
};
static_assert(sizeof(AddOrderShortMessage) == 45, "PITCH layout drift: AddOrderShortMessage");

struct AddOrderLongMessage {
  std::array<char, 8> timestamp_ms;
  char msg_type;                  // 'd'
  std::array<char, 12> order_id;
  char side;
  std::array<char, 6> shares;
  std::array<char, 8> symbol;
  std::array<char, 10> price;
  char reserved;
  std::array<char, 4> participant_id;
  char customer_indicator;
};
static_assert(sizeof(AddOrderLongMessage) == 52, "PITCH layout drift: AddOrderLongMessage");

struct AddOrderExtendedMessage {
  std::array<char, 8> timestamp_ms;
  char msg_type;                  // '1'
  std::array<char, 12> order_id;
  char side;
  std::array<char, 6> shares;
  std::array<char, 8> symbol;
  std::array<char, 14> price_long;
  char display;
  std::array<char, 4> participant_id;
  char customer_indicator;
};
static_assert(sizeof(AddOrderExtendedMessage) == 56, "PITCH layout drift: AddOrderExtendedMessage");

struct OrderExecutedMessage {
  std::array<char, 8> timestamp_ms;
  char msg_type;                  // 'E'
  std::array<char, 12> order_id;
  std::array<char, 6> executed_shares;
  std::array<char, 12> execution_id;
};
static_assert(sizeof(OrderExecutedMessage) == 39, "PITCH layout drift: OrderExecutedMessage");

struct OrderCancelMessage {
  std::array<char, 8> timestamp_ms;
  char msg_type;                  // 'X'
  std::array<char, 12> order_id;
  std::array<char, 6> canceled_shares;
};
static_assert(sizeof(OrderCancelMessage) == 27, "PITCH layout drift: OrderCancelMessage");

struct TradeShortMessage {
  std::array<char, 8> timestamp_ms;
  char msg_type;                  // 'P'
  std::array<char, 12> order_id;
  char side;
  std::array<char, 6> shares;
  std::array<char, 6> symbol;
  std::array<char, 10> price;
  std::array<char, 12> execution_id;
};
static_assert(sizeof(TradeShortMessage) == 56, "PITCH layout drift: TradeShortMessage");

struct TradeLongMessage {
  std::array<char, 8> timestamp_ms;
  char msg_type;                  // 'r'
  std::array<char, 12> order_id;
  char side;
  std::array<char, 6> shares;
  std::array<char, 8> symbol;
  std::array<char, 10> price;
  std::array<char, 12> execution_id;
};
static_assert(sizeof(TradeLongMessage) == 58, "PITCH layout drift: TradeLongMessage");

struct TradeExtendedMessage {
  std::array<char, 8> timestamp_ms;
  char msg_type;                  // '2'
  std::array<char, 12> order_id;
  char side;
  std::array<char, 6> shares;
  std::array<char, 8> symbol;
  std::array<char, 14> price_long;
  std::array<char, 12> execution_id;
};
static_assert(sizeof(TradeExtendedMessage) == 62, "PITCH layout drift: TradeExtendedMessage");

#pragma pack(pop)

}  // namespace mf::proto::cboe
