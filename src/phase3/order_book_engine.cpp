#include "mf/phase3/order_book_engine.hpp"

#include <cassert>

namespace mf::phase3 {

std::size_t OrderBookEngine::venue_index(mf::core::Venue venue) noexcept {
  const std::size_t idx = static_cast<std::size_t>(static_cast<std::uint8_t>(venue));
  assert(idx < 3);
  return idx;
}

std::uint64_t OrderBookEngine::order_key(mf::core::Venue venue, std::uint64_t order_id) noexcept {
  const std::uint64_t v = static_cast<std::uint64_t>(static_cast<std::uint8_t>(venue));
  return (v << 56U) ^ order_id;
}

TopOfBook OrderBookEngine::snapshot(const VenueBook& book) noexcept {
  TopOfBook out{};
  if (!book.bids.empty()) {
    out.has_bid = true;
    out.bid_price = book.bids.begin()->first;
    out.bid_qty = static_cast<std::uint32_t>(book.bids.begin()->second);
  }
  if (!book.asks.empty()) {
    out.has_ask = true;
    out.ask_price = book.asks.begin()->first;
    out.ask_qty = static_cast<std::uint32_t>(book.asks.begin()->second);
  }
  return out;
}

OrderBookEngine::VenueBook& OrderBookEngine::book_for(mf::core::Venue venue, std::uint64_t symbol_u64) {
  return books_[venue_index(venue)][symbol_u64];
}

const OrderBookEngine::VenueBook* OrderBookEngine::find_book(mf::core::Venue venue, std::uint64_t symbol_u64) const noexcept {
  const auto& by_symbol = books_[venue_index(venue)];
  auto it = by_symbol.find(symbol_u64);
  if (it == by_symbol.end()) {
    return nullptr;
  }
  return &it->second;
}

OrderBookEngine::ApplyResult OrderBookEngine::on_event(const mf::core::BookEvent& ev) noexcept {
  ApplyResult out{};
  const std::uint64_t symbol = ev.symbol.as_u64();
  auto& book = book_for(ev.venue, symbol);
  const TopOfBook before = snapshot(book);

  const auto add_qty = [&](mf::core::Side side, std::uint32_t price, std::uint32_t qty) {
    if (side == mf::core::Side::Buy) {
      book.bids[price] += qty;
    } else if (side == mf::core::Side::Sell) {
      book.asks[price] += qty;
    }
  };

  const auto reduce_qty = [&](mf::core::Side side, std::uint32_t price, std::uint32_t qty) {
    if (side == mf::core::Side::Buy) {
      auto it = book.bids.find(price);
      if (it == book.bids.end()) return;
      if (it->second > qty) {
        it->second -= qty;
      } else {
        book.bids.erase(it);
      }
    } else if (side == mf::core::Side::Sell) {
      auto it = book.asks.find(price);
      if (it == book.asks.end()) return;
      if (it->second > qty) {
        it->second -= qty;
      } else {
        book.asks.erase(it);
      }
    }
  };

  if (ev.type == mf::core::EventType::Add || ev.type == mf::core::EventType::AddMpid) {
    const std::uint64_t q = (ev.side == mf::core::Side::Buy) ? book.bids[ev.price] : book.asks[ev.price];
    out.queue_ahead_before_add = static_cast<double>(q);
    add_qty(ev.side, ev.price, ev.qty);
    if (ev.order_id != 0) {
      orders_[order_key(ev.venue, ev.order_id)] =
          OrderRef{ev.venue, symbol, ev.side, ev.price, ev.qty, out.queue_ahead_before_add};
    }
  } else if (
      ev.type == mf::core::EventType::Execute ||
      ev.type == mf::core::EventType::ExecutePrice ||
      ev.type == mf::core::EventType::Cancel ||
      ev.type == mf::core::EventType::Delete) {
    const std::uint64_t key = order_key(ev.venue, ev.order_id);
    auto it = orders_.find(key);
    if (it != orders_.end()) {
      const std::uint32_t dec = (ev.qty == 0 || ev.qty > it->second.qty) ? it->second.qty : ev.qty;
      reduce_qty(it->second.side, it->second.price, dec);
      if (dec >= it->second.qty || ev.type == mf::core::EventType::Delete) {
        orders_.erase(it);
      } else {
        it->second.qty -= dec;
      }
    }
  } else if (ev.type == mf::core::EventType::Replace) {
    const std::uint64_t old_key = order_key(ev.venue, ev.reference_order_id);
    auto it = orders_.find(old_key);
    if (it != orders_.end()) {
      reduce_qty(it->second.side, it->second.price, it->second.qty);
      orders_.erase(it);
    }
    add_qty(ev.side, ev.price, ev.qty);
    if (ev.order_id != 0) {
      orders_[order_key(ev.venue, ev.order_id)] = OrderRef{ev.venue, symbol, ev.side, ev.price, ev.qty, 0.0};
    }
  }

  const TopOfBook after = snapshot(book);
  out.changed_top = (
      before.has_bid != after.has_bid ||
      before.has_ask != after.has_ask ||
      before.bid_price != after.bid_price ||
      before.bid_qty != after.bid_qty ||
      before.ask_price != after.ask_price ||
      before.ask_qty != after.ask_qty);
  return out;
}

TopOfBook OrderBookEngine::top_of_book(mf::core::Venue venue, std::uint64_t symbol_u64) const noexcept {
  const auto* b = find_book(venue, symbol_u64);
  if (b == nullptr) {
    return {};
  }
  return snapshot(*b);
}

}  // namespace mf::phase3
