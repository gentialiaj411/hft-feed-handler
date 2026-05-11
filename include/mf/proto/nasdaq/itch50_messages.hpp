#pragma once

#include <array>
#include <cstdint>

namespace mf::proto::nasdaq {

#pragma pack(push, 1)

struct ItchMessageHeader {
  std::uint16_t length_be;
  char type;
};
static_assert(sizeof(ItchMessageHeader) == 3, "ITCH layout drift: ItchMessageHeader");

struct SystemEventMessage {
  std::uint16_t stock_locate_be;
  std::uint16_t tracking_number_be;
  std::uint8_t timestamp[6];
  char event_code;
};
static_assert(sizeof(SystemEventMessage) == 11, "ITCH layout drift: SystemEventMessage");

struct StockDirectoryMessage {
  std::uint16_t stock_locate_be;
  std::uint16_t tracking_number_be;
  std::uint8_t timestamp[6];
  std::array<char, 8> stock;
  char market_category;
  char financial_status;
  std::uint32_t round_lot_size_be;
  char round_lots_only;
  char issue_classification;
  std::array<char, 2> issue_sub_type;
  char authenticity;
  char short_sale_threshold_indicator;
  char ipo_flag;
  char luld_reference_price_tier;
  char etp_flag;
  std::uint32_t etp_leverage_factor_be;
  char inverse_indicator;
};
static_assert(sizeof(StockDirectoryMessage) == 38, "ITCH layout drift: StockDirectoryMessage");

struct AddOrderMessage {
  std::uint16_t stock_locate_be;
  std::uint16_t tracking_number_be;
  std::uint8_t timestamp[6];
  std::uint64_t order_ref_be;
  char buy_sell;
  std::uint32_t shares_be;
  std::array<char, 8> stock;
  std::uint32_t price_be;
};
static_assert(sizeof(AddOrderMessage) == 35, "ITCH layout drift: AddOrderMessage");

struct AddOrderMpidMessage {
  AddOrderMessage base;
  std::array<char, 4> attribution;
};
static_assert(sizeof(AddOrderMpidMessage) == 39, "ITCH layout drift: AddOrderMpidMessage");

struct OrderExecutedMessage {
  std::uint16_t stock_locate_be;
  std::uint16_t tracking_number_be;
  std::uint8_t timestamp[6];
  std::uint64_t order_ref_be;
  std::uint32_t executed_shares_be;
  std::uint64_t match_number_be;
};
static_assert(sizeof(OrderExecutedMessage) == 30, "ITCH layout drift: OrderExecutedMessage");

struct OrderExecutedPriceMessage {
  std::uint16_t stock_locate_be;
  std::uint16_t tracking_number_be;
  std::uint8_t timestamp[6];
  std::uint64_t order_ref_be;
  std::uint32_t executed_shares_be;
  std::uint64_t match_number_be;
  char printable;
  std::uint32_t execution_price_be;
};
static_assert(sizeof(OrderExecutedPriceMessage) == 35, "ITCH layout drift: OrderExecutedPriceMessage");

struct OrderCancelMessage {
  std::uint16_t stock_locate_be;
  std::uint16_t tracking_number_be;
  std::uint8_t timestamp[6];
  std::uint64_t order_ref_be;
  std::uint32_t canceled_shares_be;
};
static_assert(sizeof(OrderCancelMessage) == 22, "ITCH layout drift: OrderCancelMessage");

struct OrderDeleteMessage {
  std::uint16_t stock_locate_be;
  std::uint16_t tracking_number_be;
  std::uint8_t timestamp[6];
  std::uint64_t order_ref_be;
};
static_assert(sizeof(OrderDeleteMessage) == 18, "ITCH layout drift: OrderDeleteMessage");

struct OrderReplaceMessage {
  std::uint16_t stock_locate_be;
  std::uint16_t tracking_number_be;
  std::uint8_t timestamp[6];
  std::uint64_t original_order_ref_be;
  std::uint64_t new_order_ref_be;
  std::uint32_t shares_be;
  std::uint32_t price_be;
};
static_assert(sizeof(OrderReplaceMessage) == 34, "ITCH layout drift: OrderReplaceMessage");

struct TradeMessage {
  std::uint16_t stock_locate_be;
  std::uint16_t tracking_number_be;
  std::uint8_t timestamp[6];
  std::uint64_t order_ref_be;
  char buy_sell;
  std::uint32_t shares_be;
  std::array<char, 8> stock;
  std::uint32_t price_be;
  std::uint64_t match_number_be;
};
static_assert(sizeof(TradeMessage) == 43, "ITCH layout drift: TradeMessage");

struct CrossTradeMessage {
  std::uint16_t stock_locate_be;
  std::uint16_t tracking_number_be;
  std::uint8_t timestamp[6];
  std::uint64_t shares_be;
  std::array<char, 8> stock;
  std::uint32_t cross_price_be;
  std::uint64_t match_number_be;
  char cross_type;
};
static_assert(sizeof(CrossTradeMessage) == 39, "ITCH layout drift: CrossTradeMessage");

struct NoiiMessage {
  std::uint16_t stock_locate_be;
  std::uint16_t tracking_number_be;
  std::uint8_t timestamp[6];
  std::uint64_t paired_shares_be;
  std::uint64_t imbalance_shares_be;
  char imbalance_direction;
  std::array<char, 8> stock;
  std::uint32_t far_price_be;
  std::uint32_t near_price_be;
  std::uint32_t current_ref_price_be;
  char cross_type;
  char price_variation_indicator;
};
static_assert(sizeof(NoiiMessage) == 49, "ITCH layout drift: NoiiMessage");

#pragma pack(pop)

}  // namespace mf::proto::nasdaq
