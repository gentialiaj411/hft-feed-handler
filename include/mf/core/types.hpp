#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace mf::core {

enum class Venue : std::uint8_t {
  Nasdaq = 0,
  Iex = 1,
  Cboe = 2,
};

enum class Side : std::uint8_t {
  Buy = 0,
  Sell = 1,
  Unknown = 2,
};

enum class EventType : std::uint8_t {
  Add = 0,
  AddMpid = 1,
  Execute = 2,
  ExecutePrice = 3,
  Cancel = 4,
  Delete = 5,
  Replace = 6,
  Trade = 7,
  CrossTrade = 8,
  Imbalance = 9,
  System = 10,
  StockDirectory = 11,
  Unknown = 255,
};

struct SymbolKey {
  std::array<char, 8> bytes{};

  [[nodiscard]] constexpr std::uint64_t as_u64() const noexcept {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      value = (value << 8) | static_cast<std::uint8_t>(bytes[i]);
    }
    return value;
  }
};

struct BookEvent {
  Venue venue{Venue::Nasdaq};
  EventType type{EventType::Unknown};
  std::uint64_t sequence{0};
  std::uint64_t exchange_ts_ns{0};
  std::uint64_t ingest_ts_ns{0};
  SymbolKey symbol{};
  std::uint64_t order_id{0};
  std::uint64_t match_id{0};
  std::uint64_t reference_order_id{0};
  std::uint32_t qty{0};
  std::uint32_t price{0};
  std::uint32_t prev_qty{0};
  std::uint32_t prev_price{0};
  Side side{Side::Unknown};
  std::optional<std::array<char, 4>> mpid{};
  std::uint8_t raw_type{0};
};

}  // namespace mf::core
