#include "mf/phase3/intrusive_order_book.hpp"

#include <algorithm>
#include <cassert>

namespace mf::phase3 {

IntrusiveOrderBook::IntrusiveOrderBook(std::size_t order_capacity) {
  for (auto& by_symbol : books_) {
    by_symbol.reserve(1024);
  }
  nodes_.resize(order_capacity);
  free_list_.reserve(order_capacity);
  for (std::size_t i = order_capacity; i > 0; --i) {
    free_list_.push_back(static_cast<std::uint32_t>(i - 1U));
  }
  order_index_.reserve(order_capacity / 2U);
}

IntrusiveOrderBook::VenueBook::VenueBook() {
  bids.descending = true;
  asks.descending = false;
  bids.levels.reserve(128);
  asks.levels.reserve(128);
}

std::size_t IntrusiveOrderBook::venue_index(mf::core::Venue venue) noexcept {
  const auto idx = static_cast<std::size_t>(static_cast<std::uint8_t>(venue));
  assert(idx < mf::core::kVenueSlotCount);
  return idx;
}

std::uint64_t IntrusiveOrderBook::order_key(mf::core::Venue venue, std::uint64_t order_id) noexcept {
  return (static_cast<std::uint64_t>(static_cast<std::uint8_t>(venue)) << 56U) ^ order_id;
}

TopOfBook IntrusiveOrderBook::snapshot(const VenueBook& book) noexcept {
  TopOfBook out{};
  if (book.bids.has_best) {
    out.has_bid = true;
    out.bid_price = book.bids.best_price;
    if (auto it = book.bids.levels.find(book.bids.best_price); it != book.bids.levels.end()) {
      out.bid_qty = static_cast<std::uint32_t>(it->second.qty);
    }
  }
  if (book.asks.has_best) {
    out.has_ask = true;
    out.ask_price = book.asks.best_price;
    if (auto it = book.asks.levels.find(book.asks.best_price); it != book.asks.levels.end()) {
      out.ask_qty = static_cast<std::uint32_t>(it->second.qty);
    }
  }
  return out;
}

IntrusiveOrderBook::VenueBook& IntrusiveOrderBook::book_for(mf::core::Venue venue, std::uint64_t symbol_u64) {
  const auto idx = venue_index(venue);
  if (cached_valid_[idx] && cached_symbols_[idx] == symbol_u64 && cached_books_[idx] != nullptr) {
    return *cached_books_[idx];
  }
  auto& book = books_[idx][symbol_u64];
  cached_symbols_[idx] = symbol_u64;
  cached_books_[idx] = &book;
  cached_valid_[idx] = true;
  return book;
}

const IntrusiveOrderBook::VenueBook* IntrusiveOrderBook::find_book(
    mf::core::Venue venue,
    std::uint64_t symbol_u64) const noexcept {
  const auto& by_symbol = books_[venue_index(venue)];
  const auto it = by_symbol.find(symbol_u64);
  return it == by_symbol.end() ? nullptr : &it->second;
}

IntrusiveOrderBook::SideBook& IntrusiveOrderBook::side_book(VenueBook& book, mf::core::Side side) noexcept {
  return side == mf::core::Side::Buy ? book.bids : book.asks;
}

const IntrusiveOrderBook::SideBook& IntrusiveOrderBook::side_book(const VenueBook& book, mf::core::Side side) const noexcept {
  return side == mf::core::Side::Buy ? book.bids : book.asks;
}

IntrusiveOrderBook::Level& IntrusiveOrderBook::find_or_add_level(SideBook& side, std::uint32_t price) {
  auto [it, inserted] = side.levels.emplace(price, Level{price, 0, kNull, kNull});
  if (inserted) {
    update_best_on_add(side, price);
  }
  return it->second;
}

void IntrusiveOrderBook::remove_level_if_empty(SideBook& side, std::uint32_t price) noexcept {
  auto it = side.levels.find(price);
  if (it == side.levels.end() || it->second.qty != 0 || it->second.head != kNull) {
    return;
  }
  const bool removed_best = side.has_best && side.best_price == price;
  side.levels.erase(it);
  if (removed_best) {
    recompute_best(side);
  }
}

void IntrusiveOrderBook::update_best_on_add(SideBook& side, std::uint32_t price) noexcept {
  if (!side.has_best) {
    side.best_price = price;
    side.has_best = true;
    return;
  }
  if ((side.descending && price > side.best_price) || (!side.descending && price < side.best_price)) {
    side.best_price = price;
  }
}

void IntrusiveOrderBook::recompute_best(SideBook& side) noexcept {
  side.has_best = false;
  side.best_price = 0;
  for (const auto& [price, level] : side.levels) {
    if (level.qty == 0) {
      continue;
    }
    update_best_on_add(side, price);
  }
}

std::uint32_t IntrusiveOrderBook::allocate_node() noexcept {
  if (free_list_.empty()) {
    ++dropped_order_inserts_;
    return kNull;
  }
  const auto idx = free_list_.back();
  free_list_.pop_back();
  nodes_[idx] = OrderNode{};
  nodes_[idx].live = true;
  ++live_order_count_;
  return idx;
}

void IntrusiveOrderBook::release_node(std::uint32_t idx) noexcept {
  if (idx == kNull || idx >= nodes_.size() || !nodes_[idx].live) {
    return;
  }
  nodes_[idx] = OrderNode{};
  free_list_.push_back(idx);
  if (live_order_count_ > 0) {
    --live_order_count_;
  }
}

bool IntrusiveOrderBook::add_order(
    mf::core::Venue venue,
    std::uint64_t symbol,
    const mf::core::BookEvent& ev,
    double& queue_ahead) noexcept {
  if (ev.side != mf::core::Side::Buy && ev.side != mf::core::Side::Sell) {
    return true;
  }
  const auto idx = allocate_node();
  if (idx == kNull) {
    return false;
  }

  auto& book = book_for(venue, symbol);
  auto& side = side_book(book, ev.side);
  auto& level = find_or_add_level(side, ev.price);
  queue_ahead = static_cast<double>(level.qty);

  auto& node = nodes_[idx];
  node.key = order_key(venue, ev.order_id);
  node.symbol_u64 = symbol;
  node.price = ev.price;
  node.qty = ev.qty;
  node.venue = venue;
  node.side = ev.side;
  node.prev = level.tail;

  if (level.tail != kNull) {
    nodes_[level.tail].next = idx;
  } else {
    level.head = idx;
  }
  level.tail = idx;
  level.qty += ev.qty;
  order_index_[node.key] = idx;
  return true;
}

void IntrusiveOrderBook::reduce_order(std::uint64_t key, std::uint32_t qty, bool delete_all) noexcept {
  const auto it = order_index_.find(key);
  if (it == order_index_.end()) {
    return;
  }
  const auto idx = it->second;
  auto& node = nodes_[idx];
  auto& book = book_for(node.venue, node.symbol_u64);
  auto& side = side_book(book, node.side);
  auto level_it = side.levels.find(node.price);
  if (level_it == side.levels.end()) {
    order_index_.erase(it);
    release_node(idx);
    return;
  }

  auto& level = level_it->second;
  const std::uint32_t dec = (delete_all || qty == 0 || qty > node.qty) ? node.qty : qty;
  level.qty = (level.qty > dec) ? (level.qty - dec) : 0;
  node.qty -= dec;
  if (node.qty == 0 || delete_all) {
    if (node.prev != kNull) {
      nodes_[node.prev].next = node.next;
    } else {
      level.head = node.next;
    }
    if (node.next != kNull) {
      nodes_[node.next].prev = node.prev;
    } else {
      level.tail = node.prev;
    }
    const auto old_price = node.price;
    order_index_.erase(it);
    release_node(idx);
    remove_level_if_empty(side, old_price);
  }
}

void IntrusiveOrderBook::replace_order(
    mf::core::Venue venue,
    std::uint64_t symbol,
    const mf::core::BookEvent& ev) noexcept {
  mf::core::Side side = ev.side;
  const auto old_key = order_key(venue, ev.reference_order_id);
  if (auto it = order_index_.find(old_key); it != order_index_.end()) {
    if (side == mf::core::Side::Unknown) {
      side = nodes_[it->second].side;
    }
    reduce_order(old_key, 0, true);
  }
  auto add_ev = ev;
  add_ev.side = side;
  double ignored = 0.0;
  (void)add_order(venue, symbol, add_ev, ignored);
}

IntrusiveOrderBook::ApplyResult IntrusiveOrderBook::on_event(const mf::core::BookEvent& ev) noexcept {
  ApplyResult out{};
  const auto symbol = ev.symbol.as_u64();
  auto& book = book_for(ev.venue, symbol);
  const auto before = snapshot(book);

  if (ev.type == mf::core::EventType::Add || ev.type == mf::core::EventType::AddMpid) {
    if (ev.order_id != 0) {
      out.order_pool_exhausted = !add_order(ev.venue, symbol, ev, out.queue_ahead_before_add);
    }
  } else if (
      ev.type == mf::core::EventType::Execute ||
      ev.type == mf::core::EventType::ExecutePrice ||
      ev.type == mf::core::EventType::Cancel ||
      ev.type == mf::core::EventType::Delete) {
    reduce_order(order_key(ev.venue, ev.order_id), ev.qty, ev.type == mf::core::EventType::Delete);
  } else if (ev.type == mf::core::EventType::Replace) {
    replace_order(ev.venue, symbol, ev);
  }

  const auto after = snapshot(book);
  out.top_after = after;
  out.changed_top =
      before.has_bid != after.has_bid ||
      before.has_ask != after.has_ask ||
      before.bid_price != after.bid_price ||
      before.bid_qty != after.bid_qty ||
      before.ask_price != after.ask_price ||
      before.ask_qty != after.ask_qty;
  return out;
}

TopOfBook IntrusiveOrderBook::top_of_book(mf::core::Venue venue, std::uint64_t symbol_u64) const noexcept {
  const auto* book = find_book(venue, symbol_u64);
  return book == nullptr ? TopOfBook{} : snapshot(*book);
}

BookSnapshot IntrusiveOrderBook::snapshot(mf::core::Venue venue, std::uint64_t symbol_u64) const {
  BookSnapshot out{};
  const auto* book = find_book(venue, symbol_u64);
  if (book == nullptr) {
    return out;
  }
  out.bids.reserve(book->bids.levels.size());
  for (const auto& [price, level] : book->bids.levels) {
    out.bids.push_back(BookLevel{price, level.qty});
  }
  std::sort(out.bids.begin(), out.bids.end(), [](const BookLevel& a, const BookLevel& b) {
    return a.price > b.price;
  });
  out.asks.reserve(book->asks.levels.size());
  for (const auto& [price, level] : book->asks.levels) {
    out.asks.push_back(BookLevel{price, level.qty});
  }
  std::sort(out.asks.begin(), out.asks.end(), [](const BookLevel& a, const BookLevel& b) {
    return a.price < b.price;
  });
  return out;
}

}  // namespace mf::phase3
