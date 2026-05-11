#pragma once

#include <array>
#include <cstdint>

namespace mf::proto::nyse {

#pragma pack(push, 1)

// NOTE: These layouts are intentionally minimal Phase-1 scaffolds until full
// exchange-published Pillar Integrated field maps are pinned in-repo.
struct CommonHeader {
  std::uint16_t msg_size_le;
  std::uint16_t msg_type_le;
  std::uint32_t source_time_ns_le;
  std::uint32_t symbol_index_le;
  std::uint32_t source_seq_le;
};
static_assert(sizeof(CommonHeader) == 16, "Pillar layout drift: CommonHeader");

struct AddOrderMessage {
  CommonHeader h;
  std::uint64_t order_id_le;
  std::uint32_t price_le;
  std::uint32_t qty_le;
  char side;
  std::array<char, 8> symbol;
};
static_assert(sizeof(AddOrderMessage) == 41, "Pillar layout drift: AddOrderMessage");

struct ModifyOrderMessage {
  CommonHeader h;
  std::uint64_t order_id_le;
  std::uint32_t new_price_le;
  std::uint32_t new_qty_le;
};
static_assert(sizeof(ModifyOrderMessage) == 32, "Pillar layout drift: ModifyOrderMessage");

struct DeleteOrderMessage {
  CommonHeader h;
  std::uint64_t order_id_le;
};
static_assert(sizeof(DeleteOrderMessage) == 24, "Pillar layout drift: DeleteOrderMessage");

struct ExecutionMessage {
  CommonHeader h;
  std::uint64_t order_id_le;
  std::uint32_t executed_qty_le;
  std::uint64_t match_id_le;
};
static_assert(sizeof(ExecutionMessage) == 36, "Pillar layout drift: ExecutionMessage");

struct TradeMessage {
  CommonHeader h;
  std::uint64_t trade_id_le;
  std::uint32_t price_le;
  std::uint32_t qty_le;
  char side;
  std::array<char, 8> symbol;
};
static_assert(sizeof(TradeMessage) == 41, "Pillar layout drift: TradeMessage");

#pragma pack(pop)

}  // namespace mf::proto::nyse
