#pragma once

#include <array>
#include <cstdint>

namespace mf::proto::iex {

#pragma pack(push, 1)

struct CommonHeader {
  std::uint16_t msg_size_le;
  std::uint16_t msg_type_le;
  std::uint32_t source_time_ns_le;
  std::uint32_t symbol_index_le;
  std::uint32_t symbol_seq_num_le;
};
static_assert(sizeof(CommonHeader) == 16, "IEX DEEP layout drift: CommonHeader");

// Msg Type 100, size 39
struct AddOrderMessage {
  CommonHeader h;
  std::uint64_t order_id_le;
  std::uint32_t price_le;
  std::uint32_t volume_le;
  char side;
  std::array<char, 5> firm_id;
  std::uint8_t reserved_1;
};
static_assert(sizeof(AddOrderMessage) == 39, "IEX DEEP layout drift: AddOrderMessage");

// Msg Type 101, size 35
struct ModifyOrderMessage {
  CommonHeader h;
  std::uint64_t order_id_le;
  std::uint32_t price_le;
  std::uint32_t volume_le;
  std::uint8_t position_change;
  std::uint8_t reserved_1;
  std::uint8_t reserved_2;
};
static_assert(sizeof(ModifyOrderMessage) == 35, "IEX DEEP layout drift: ModifyOrderMessage");

// Msg Type 102, size 25
struct DeleteOrderMessage {
  CommonHeader h;
  std::uint64_t order_id_le;
  std::uint8_t reserved_1;
};
static_assert(sizeof(DeleteOrderMessage) == 25, "IEX DEEP layout drift: DeleteOrderMessage");

// Msg Type 103, size 42
struct OrderExecutionMessage {
  CommonHeader h;
  std::uint64_t order_id_le;
  std::uint32_t trade_id_le;
  std::uint32_t price_le;
  std::uint32_t volume_le;
  std::uint8_t printable_flag;
  std::uint8_t reserved_1;
  char trade_cond_1;
  char trade_cond_2;
  char trade_cond_3;
  char trade_cond_4;
};
static_assert(sizeof(OrderExecutionMessage) == 42, "IEX DEEP layout drift: OrderExecutionMessage");

// Msg Type 110, size 33
struct NonDisplayedTradeMessage {
  CommonHeader h;
  std::uint32_t trade_id_le;
  std::uint32_t price_le;
  std::uint32_t volume_le;
  std::uint8_t printable_flag;
  char trade_cond_1;
  char trade_cond_2;
  char trade_cond_3;
  char trade_cond_4;
};
static_assert(sizeof(NonDisplayedTradeMessage) == 33, "IEX DEEP layout drift: NonDisplayedTradeMessage");

#pragma pack(pop)

}  // namespace mf::proto::iex
