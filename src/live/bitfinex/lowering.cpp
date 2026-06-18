#include "mf/live/bitfinex/lowering.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mf::live::bitfinex {

namespace {

mf::core::Side side_from_amount(double amount) noexcept {
  if (amount > 0.0) return mf::core::Side::Buy;
  if (amount < 0.0) return mf::core::Side::Sell;
  return mf::core::Side::Unknown;
}

mf::core::BookEvent make_base(std::uint64_t sequence, std::uint64_t ts_ns, const mf::core::SymbolKey& sym) noexcept {
  mf::core::BookEvent ev{};
  ev.venue = mf::core::Venue::Bitfinex;
  ev.sequence = sequence;
  ev.exchange_ts_ns = ts_ns;
  ev.ingest_ts_ns = ts_ns;
  ev.symbol = sym;
  return ev;
}

}  // namespace

void symbol_from_wire(const std::string& wire, mf::core::SymbolKey& out) noexcept {
  out = {};
  const std::size_t n = std::min(wire.size(), out.bytes.size());
  std::memcpy(out.bytes.data(), wire.data(), n);
}

std::optional<mf::core::BookEvent> lower_row(
    const BookRow& row,
    std::uint64_t sequence,
    std::uint64_t ts_ns,
    const std::string& symbol_wire,
    OnBookTracker& on_book,
    LoweringStats& stats) noexcept {
  mf::core::SymbolKey sym{};
  symbol_from_wire(symbol_wire, sym);

  const bool removal = row.price == 0.0;
  const mf::core::Side side = side_from_amount(row.amount);

  if (removal) {
    if (!on_book.contains(row.order_id)) {
      ++stats.skipped_not_on_book;
      return std::nullopt;
    }
    auto ev = make_base(sequence, ts_ns, sym);
    ev.type = mf::core::EventType::Cancel;
    ev.raw_type = static_cast<std::uint8_t>('X');
    ev.order_id = row.order_id;
    ev.side = side;
    ev.qty = 0;
    on_book.erase(row.order_id);
    ++stats.cancels;
    return ev;
  }

  std::uint32_t price_u32 = 0;
  std::uint32_t qty_u32 = 0;
  if (!price_to_u32_4dp(row.price, price_u32, stats.fixed_point) ||
      !qty_to_u32_micro_base(std::abs(row.amount), qty_u32, stats.fixed_point)) {
    ++stats.rejected;
    return std::nullopt;
  }

  if (!on_book.contains(row.order_id)) {
    auto ev = make_base(sequence, ts_ns, sym);
    ev.type = mf::core::EventType::Add;
    ev.raw_type = static_cast<std::uint8_t>('A');
    ev.order_id = row.order_id;
    ev.side = side;
    ev.price = price_u32;
    ev.qty = qty_u32;
    on_book.insert(row.order_id, price_u32, qty_u32);
    ++stats.adds;
    return ev;
  }

  const auto prev = on_book.get(row.order_id);
  auto ev = make_base(sequence, ts_ns, sym);
  ev.type = mf::core::EventType::Replace;
  ev.raw_type = static_cast<std::uint8_t>('R');
  ev.order_id = row.order_id;
  ev.reference_order_id = row.order_id;
  ev.side = side;
  ev.price = price_u32;
  ev.qty = qty_u32;
  if (prev.has_value()) {
    ev.prev_price = prev->price;
    ev.prev_qty = prev->qty;
  }
  on_book.update(row.order_id, price_u32, qty_u32);
  ++stats.replaces;
  return ev;
}

std::vector<mf::core::BookEvent> lower_snapshot_rows(
    const std::vector<BookRow>& rows,
    std::uint64_t ts_ns,
    const std::string& symbol_wire,
    OnBookTracker& on_book,
    LoweringStats& stats) noexcept {
  std::vector<mf::core::BookEvent> out{};
  out.reserve(rows.size());
  for (const auto& row : rows) {
    if (row.price == 0.0) continue;
    mf::core::SymbolKey sym{};
    symbol_from_wire(symbol_wire, sym);
    std::uint32_t price_u32 = 0;
    std::uint32_t qty_u32 = 0;
    if (!price_to_u32_4dp(row.price, price_u32, stats.fixed_point) ||
        !qty_to_u32_micro_base(std::abs(row.amount), qty_u32, stats.fixed_point)) {
      ++stats.rejected;
      continue;
    }
    auto ev = make_base(0, ts_ns, sym);
    ev.type = mf::core::EventType::Add;
    ev.raw_type = static_cast<std::uint8_t>('A');
    ev.order_id = row.order_id;
    ev.side = side_from_amount(row.amount);
    ev.price = price_u32;
    ev.qty = qty_u32;
    on_book.insert(row.order_id, price_u32, qty_u32);
    out.push_back(ev);
    ++stats.adds;
  }
  return out;
}

}  // namespace mf::live::bitfinex
