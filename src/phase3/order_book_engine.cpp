#include "mf/phase3/order_book_engine.hpp"

#include <algorithm>
#include <cassert>

namespace mf::phase3 {

OrderBookEngine::OrderBookEngine() {
  for (auto& by_symbol : books_) {
    by_symbol.reserve(1024);
  }
  orders_.resize(1U << 21U);
  order_mask_ = orders_.size() - 1U;
}

OrderBookEngine::VenueBook::VenueBook() {
  bids.reserve(128);
  asks.reserve(128);
}

std::size_t OrderBookEngine::venue_index(mf::core::Venue venue) noexcept {
  const std::size_t idx = static_cast<std::size_t>(static_cast<std::uint8_t>(venue));
  assert(idx < mf::core::kVenueSlotCount);
  return idx;
}

std::uint64_t OrderBookEngine::order_key(mf::core::Venue venue, std::uint64_t order_id) noexcept {
  const std::uint64_t v = static_cast<std::uint64_t>(static_cast<std::uint8_t>(venue));
  return (v << 56U) ^ order_id;
}

std::uint64_t OrderBookEngine::mix_key(std::uint64_t key) noexcept {
  key ^= key >> 33U;
  key *= 0xff51afd7ed558ccdULL;
  key ^= key >> 33U;
  key *= 0xc4ceb9fe1a85ec53ULL;
  key ^= key >> 33U;
  return key;
}

TopOfBook OrderBookEngine::snapshot(const VenueBook& book) noexcept {
  TopOfBook out{};
  if (!book.bids.empty()) {
    out.has_bid = true;
    out.bid_price = book.bids.front().first;
    out.bid_qty = static_cast<std::uint32_t>(book.bids.front().second);
  }
  if (!book.asks.empty()) {
    out.has_ask = true;
    out.ask_price = book.asks.front().first;
    out.ask_qty = static_cast<std::uint32_t>(book.asks.front().second);
  }
  return out;
}

std::uint64_t OrderBookEngine::qty_at(
    const std::vector<VenueBook::Level>& levels,
    std::uint32_t price,
    bool descending) noexcept {
  const auto it = std::lower_bound(
      levels.begin(),
      levels.end(),
      price,
      [descending](const VenueBook::Level& level, std::uint32_t value) {
        return descending ? (level.first > value) : (level.first < value);
      });
  if (it == levels.end() || it->first != price) {
    return 0;
  }
  return it->second;
}

void OrderBookEngine::add_qty(
    std::vector<VenueBook::Level>& levels,
    std::uint32_t price,
    std::uint32_t qty,
    bool descending) {
  const auto it = std::lower_bound(
      levels.begin(),
      levels.end(),
      price,
      [descending](const VenueBook::Level& level, std::uint32_t value) {
        return descending ? (level.first > value) : (level.first < value);
      });
  if (it != levels.end() && it->first == price) {
    it->second += qty;
    return;
  }
  levels.insert(it, VenueBook::Level{price, qty});
}

void OrderBookEngine::reduce_qty(
    std::vector<VenueBook::Level>& levels,
    std::uint32_t price,
    std::uint32_t qty,
    bool descending) noexcept {
  const auto it = std::lower_bound(
      levels.begin(),
      levels.end(),
      price,
      [descending](const VenueBook::Level& level, std::uint32_t value) {
        return descending ? (level.first > value) : (level.first < value);
      });
  if (it == levels.end() || it->first != price) {
    return;
  }
  if (it->second > qty) {
    it->second -= qty;
  } else {
    levels.erase(it);
  }
}

OrderBookEngine::VenueBook& OrderBookEngine::book_for(mf::core::Venue venue, std::uint64_t symbol_u64) {
  const std::size_t idx = venue_index(venue);
  if (cached_valid_[idx] && cached_symbols_[idx] == symbol_u64 && cached_books_[idx] != nullptr) {
    return *cached_books_[idx];
  }

  auto& book = books_[idx][symbol_u64];
  cached_symbols_[idx] = symbol_u64;
  cached_books_[idx] = &book;
  cached_valid_[idx] = true;
  return book;
}

const OrderBookEngine::VenueBook* OrderBookEngine::find_book(mf::core::Venue venue, std::uint64_t symbol_u64) const noexcept {
  const auto& by_symbol = books_[venue_index(venue)];
  auto it = by_symbol.find(symbol_u64);
  if (it == by_symbol.end()) {
    return nullptr;
  }
  return &it->second;
}

OrderBookEngine::OrderRef* OrderBookEngine::find_order(std::uint64_t key) noexcept {
  std::size_t idx = static_cast<std::size_t>(mix_key(key)) & order_mask_;
  for (std::size_t probes = 0; probes < orders_.size(); ++probes) {
    auto& slot = orders_[idx];
    if (slot.state == 0U) {
      return nullptr;
    }
    if (slot.state == 1U && slot.key == key) {
      return &slot.value;
    }
    idx = (idx + 1U) & order_mask_;
  }
  return nullptr;
}

bool OrderBookEngine::put_order(std::uint64_t key, const OrderRef& value) noexcept {
  if (order_count_ > (orders_.size() / 2U)) {
#ifndef NDEBUG
    assert(false && "order table hard limit: live orders must stay at or below 50% load");
#endif
    ++dropped_order_inserts_;
    return false;
  }
  std::size_t idx = static_cast<std::size_t>(mix_key(key)) & order_mask_;
  std::size_t first_tombstone = orders_.size();
  for (std::size_t probes = 0; probes < orders_.size(); ++probes) {
    auto& slot = orders_[idx];
    if (slot.state == 1U && slot.key == key) {
      slot.value = value;
      return true;
    }
    if (slot.state == 2U && first_tombstone == orders_.size()) {
      first_tombstone = idx;
    }
    if (slot.state == 0U) {
      auto& target = (first_tombstone == orders_.size()) ? slot : orders_[first_tombstone];
      target.key = key;
      target.value = value;
      target.state = 1U;
      ++order_count_;
      return true;
    }
    idx = (idx + 1U) & order_mask_;
  }
  ++dropped_order_inserts_;
  return false;
}

void OrderBookEngine::erase_order(std::uint64_t key) noexcept {
  std::size_t idx = static_cast<std::size_t>(mix_key(key)) & order_mask_;
  for (std::size_t probes = 0; probes < orders_.size(); ++probes) {
    auto& slot = orders_[idx];
    if (slot.state == 0U) {
      return;
    }
    if (slot.state == 1U && slot.key == key) {
      slot.state = 2U;
      if (order_count_ > 0U) {
        --order_count_;
      }
      return;
    }
    idx = (idx + 1U) & order_mask_;
  }
}

OrderBookEngine::ApplyResult OrderBookEngine::on_event(const mf::core::BookEvent& ev) noexcept {
  ApplyResult out{};
  const std::uint64_t symbol = ev.symbol.as_u64();
  auto& book = book_for(ev.venue, symbol);
  const TopOfBook before = snapshot(book);

  if (ev.type == mf::core::EventType::Add || ev.type == mf::core::EventType::AddMpid) {
    const bool is_bid = ev.side == mf::core::Side::Buy;
    const std::uint64_t q = is_bid ? qty_at(book.bids, ev.price, /*descending=*/true)
                                   : qty_at(book.asks, ev.price, /*descending=*/false);
    out.queue_ahead_before_add = static_cast<double>(q);
    if (is_bid) {
      add_qty(book.bids, ev.price, ev.qty, /*descending=*/true);
    } else if (ev.side == mf::core::Side::Sell) {
      add_qty(book.asks, ev.price, ev.qty, /*descending=*/false);
    }
    if (ev.order_id != 0) {
      out.order_table_saturated = !put_order(
          order_key(ev.venue, ev.order_id),
          OrderRef{ev.venue, symbol, ev.side, ev.price, ev.qty, out.queue_ahead_before_add});
    }
  } else if (
      ev.type == mf::core::EventType::Execute ||
      ev.type == mf::core::EventType::ExecutePrice ||
      ev.type == mf::core::EventType::Cancel ||
      ev.type == mf::core::EventType::Delete) {
    const std::uint64_t key = order_key(ev.venue, ev.order_id);
    if (auto* order = find_order(key); order != nullptr) {
      const std::uint32_t dec = (ev.qty == 0 || ev.qty > order->qty) ? order->qty : ev.qty;
      if (order->side == mf::core::Side::Buy) {
        reduce_qty(book.bids, order->price, dec, /*descending=*/true);
      } else if (order->side == mf::core::Side::Sell) {
        reduce_qty(book.asks, order->price, dec, /*descending=*/false);
      }
      if (dec >= order->qty || ev.type == mf::core::EventType::Delete) {
        erase_order(key);
      } else {
        order->qty -= dec;
      }
    }
  } else if (ev.type == mf::core::EventType::Replace) {
    const std::uint64_t old_key = order_key(ev.venue, ev.reference_order_id);
    if (auto* order = find_order(old_key); order != nullptr) {
      if (order->side == mf::core::Side::Buy) {
        reduce_qty(book.bids, order->price, order->qty, /*descending=*/true);
      } else if (order->side == mf::core::Side::Sell) {
        reduce_qty(book.asks, order->price, order->qty, /*descending=*/false);
      }
      erase_order(old_key);
    }
    if (ev.side == mf::core::Side::Buy) {
      add_qty(book.bids, ev.price, ev.qty, /*descending=*/true);
    } else if (ev.side == mf::core::Side::Sell) {
      add_qty(book.asks, ev.price, ev.qty, /*descending=*/false);
    }
    if (ev.order_id != 0) {
      out.order_table_saturated =
          !put_order(order_key(ev.venue, ev.order_id), OrderRef{ev.venue, symbol, ev.side, ev.price, ev.qty, 0.0});
    }
  }

  const TopOfBook after = snapshot(book);
  out.top_after = after;
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

BookSnapshot OrderBookEngine::snapshot(mf::core::Venue venue, std::uint64_t symbol_u64) const {
  BookSnapshot out{};
  const auto* b = find_book(venue, symbol_u64);
  if (b == nullptr) {
    return out;
  }
  out.bids.reserve(b->bids.size());
  for (const auto& level : b->bids) {
    out.bids.push_back(BookLevel{level.first, level.second});
  }
  out.asks.reserve(b->asks.size());
  for (const auto& level : b->asks) {
    out.asks.push_back(BookLevel{level.first, level.second});
  }
  return out;
}

}  // namespace mf::phase3
