#pragma once

#include <array>
#include <cstdint>

namespace mf::proto::cboe {

#pragma pack(push, 1)

// NOTE: Phase-1 scaffold layouts pending exact exchange spec pin.
struct CommonHeader {
  std::uint8_t length;
  std::uint8_t msg_type;
  std::uint32_t time_offset_ns_le;
};
static_assert(sizeof(CommonHeader) == 6, "PITCH layout drift: CommonHeader");

struct AddOrderMessage {
  CommonHeader h;
  std::uint64_t order_id_le;
  char side;
  std::uint32_t qty_le;
  std::array<char, 8> symbol;
  std::uint32_t price_le;
};
static_assert(sizeof(AddOrderMessage) == 31, "PITCH layout drift: AddOrderMessage");

struct OrderExecutedMessage {
  CommonHeader h;
  std::uint64_t order_id_le;
  std::uint32_t executed_qty_le;
  std::uint64_t match_id_le;
};
static_assert(sizeof(OrderExecutedMessage) == 26, "PITCH layout drift: OrderExecutedMessage");

struct OrderCancelMessage {
  CommonHeader h;
  std::uint64_t order_id_le;
  std::uint32_t canceled_qty_le;
};
static_assert(sizeof(OrderCancelMessage) == 18, "PITCH layout drift: OrderCancelMessage");

struct OrderModifyMessage {
  CommonHeader h;
  std::uint64_t order_id_le;
  std::uint32_t new_qty_le;
  std::uint32_t new_price_le;
};
static_assert(sizeof(OrderModifyMessage) == 22, "PITCH layout drift: OrderModifyMessage");

struct TradeMessage {
  CommonHeader h;
  std::uint64_t trade_id_le;
  char side;
  std::uint32_t qty_le;
  std::array<char, 8> symbol;
  std::uint32_t price_le;
};
static_assert(sizeof(TradeMessage) == 31, "PITCH layout drift: TradeMessage");

#pragma pack(pop)

}  // namespace mf::proto::cboe
