#pragma once

#include <array>
#include <cstdint>

namespace mf::proto::iex {

#pragma pack(push, 1)

struct SystemEventMessage {
  char msg_type;                 // 'S'
  char system_event;
  std::uint64_t timestamp_le;
};
static_assert(sizeof(SystemEventMessage) == 10, "IEX DEEP+ layout drift: SystemEventMessage");

struct SecurityEventMessage {
  char msg_type;                 // 'E'
  char security_event;
  std::uint64_t timestamp_le;
};
static_assert(sizeof(SecurityEventMessage) == 10, "IEX DEEP+ layout drift: SecurityEventMessage");

// IEX DEEP+ v1.02, Trading Message Formats (pages 16-22)
struct AddOrderMessage {
  char msg_type;                 // 'a'
  char side;                     // '8' buy, '5' sell
  std::uint64_t timestamp_le;
  std::array<char, 8> symbol;
  std::uint64_t order_id_le;
  std::uint32_t size_le;
  std::uint64_t price_le;
};
static_assert(sizeof(AddOrderMessage) == 38, "IEX DEEP+ layout drift: AddOrderMessage");

struct ModifyOrderMessage {
  char msg_type;                 // 'M'
  std::uint8_t modify_flags;
  std::uint64_t timestamp_le;
  std::array<char, 8> symbol;
  std::uint64_t order_id_ref_le;
  std::uint32_t size_le;
  std::uint64_t price_le;
};
static_assert(sizeof(ModifyOrderMessage) == 38, "IEX DEEP+ layout drift: ModifyOrderMessage");

struct DeleteOrderMessage {
  char msg_type;                 // 'R'
  std::uint8_t reserved;
  std::uint64_t timestamp_le;
  std::array<char, 8> symbol;
  std::uint64_t order_id_ref_le;
};
static_assert(sizeof(DeleteOrderMessage) == 26, "IEX DEEP+ layout drift: DeleteOrderMessage");

struct OrderExecutedMessage {
  char msg_type;                 // 'L'
  std::uint8_t sale_condition_flags;
  std::uint64_t timestamp_le;
  std::array<char, 8> symbol;
  std::uint64_t order_id_ref_le;
  std::uint32_t size_le;
  std::uint64_t price_le;
  std::uint64_t trade_id_le;
};
static_assert(sizeof(OrderExecutedMessage) == 46, "IEX DEEP+ layout drift: OrderExecutedMessage");

struct TradeMessage {
  char msg_type;                 // 'T'
  std::uint8_t sale_condition_flags;
  std::uint64_t timestamp_le;
  std::array<char, 8> symbol;
  std::uint32_t size_le;
  std::uint64_t price_le;
  std::uint64_t trade_id_le;
};
static_assert(sizeof(TradeMessage) == 38, "IEX DEEP+ layout drift: TradeMessage");

#pragma pack(pop)

}  // namespace mf::proto::iex
