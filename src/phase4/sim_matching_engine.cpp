#include "mf/phase4/sim_matching_engine.hpp"

#include <algorithm>
#include <cmath>

namespace mf::phase4 {

SimMatchingEngine::SimMatchingEngine(double participation_cap)
    : participation_cap_(participation_cap) {
  books_.reserve(256);
  order_to_symbol_.reserve(4096);
}

bool SimMatchingEngine::bid_better(const Order& a, const Order& b) noexcept {
  if (a.price != b.price) {
    return a.price > b.price;
  }
  return a.ts_ns < b.ts_ns;
}

bool SimMatchingEngine::ask_better(const Order& a, const Order& b) noexcept {
  if (a.price != b.price) {
    return a.price < b.price;
  }
  return a.ts_ns < b.ts_ns;
}

void SimMatchingEngine::bind_order_symbol(std::uint64_t order_id, std::uint64_t symbol_u64) {
  if (order_id == 0) {
    return;
  }
  order_to_symbol_[order_id] = symbol_u64;
}

void SimMatchingEngine::submit(OrderSide side, std::int64_t price, std::uint64_t qty, std::uint64_t order_id, std::uint64_t ts_ns) {
  if (order_id == 0 || qty == 0) {
    return;
  }
  auto it = order_to_symbol_.find(order_id);
  if (it == order_to_symbol_.end()) {
    return;
  }
  auto& book = books_[it->second];
  Order ord{order_id, side, price, qty, ts_ns};
  auto& side_vec = (side == OrderSide::Buy) ? book.bids : book.asks;
  side_vec.push_back(ord);
  if (side == OrderSide::Buy) {
    std::sort(side_vec.begin(), side_vec.end(), &SimMatchingEngine::bid_better);
  } else {
    std::sort(side_vec.begin(), side_vec.end(), &SimMatchingEngine::ask_better);
  }
}

void SimMatchingEngine::cancel(std::uint64_t order_id, std::uint64_t) {
  auto it = order_to_symbol_.find(order_id);
  if (it == order_to_symbol_.end()) {
    return;
  }
  auto book_it = books_.find(it->second);
  if (book_it == books_.end()) {
    return;
  }
  auto remove_order = [order_id](std::vector<Order>& v) {
    v.erase(std::remove_if(v.begin(), v.end(), [order_id](const Order& o) { return o.order_id == order_id; }), v.end());
  };
  remove_order(book_it->second.bids);
  remove_order(book_it->second.asks);
  order_to_symbol_.erase(it);
}

void SimMatchingEngine::fill_bids(SymbolBook& book, std::int64_t trade_price, std::uint64_t fill_budget, std::uint64_t ts_ns) {
  for (auto it = book.bids.begin(); it != book.bids.end() && fill_budget > 0;) {
    if (it->price < trade_price) {
      break;
    }
    const std::uint64_t take = std::min(it->qty, fill_budget);
    if (on_fill_ != nullptr && take > 0) {
      on_fill_(Fill{it->order_id, OrderSide::Buy, trade_price, take, ts_ns, take < it->qty});
    }
    it->qty -= take;
    fill_budget -= take;
    if (it->qty == 0) {
      order_to_symbol_.erase(it->order_id);
      it = book.bids.erase(it);
    } else {
      ++it;
    }
  }
}

void SimMatchingEngine::fill_asks(SymbolBook& book, std::int64_t trade_price, std::uint64_t fill_budget, std::uint64_t ts_ns) {
  for (auto it = book.asks.begin(); it != book.asks.end() && fill_budget > 0;) {
    if (it->price > trade_price) {
      break;
    }
    const std::uint64_t take = std::min(it->qty, fill_budget);
    if (on_fill_ != nullptr && take > 0) {
      on_fill_(Fill{it->order_id, OrderSide::Sell, trade_price, take, ts_ns, take < it->qty});
    }
    it->qty -= take;
    fill_budget -= take;
    if (it->qty == 0) {
      order_to_symbol_.erase(it->order_id);
      it = book.asks.erase(it);
    } else {
      ++it;
    }
  }
}

void SimMatchingEngine::on_market_event(const mf::core::BookEvent& ev) {
  if (!(ev.type == mf::core::EventType::Trade || ev.type == mf::core::EventType::ExecutePrice || ev.type == mf::core::EventType::CrossTrade)) {
    return;
  }
  const std::uint64_t symbol = ev.symbol.as_u64();
  auto it = books_.find(symbol);
  if (it == books_.end() || ev.qty == 0) {
    return;
  }
  const std::uint64_t budget = static_cast<std::uint64_t>(
      std::floor(static_cast<double>(ev.qty) * ((participation_cap_ > 0.0) ? participation_cap_ : 0.0)));
  if (budget == 0) {
    return;
  }
  const std::int64_t trade_price = static_cast<std::int64_t>(ev.price);
  if (ev.side == mf::core::Side::Sell) {
    fill_bids(it->second, trade_price, budget, ev.exchange_ts_ns);
  } else if (ev.side == mf::core::Side::Buy) {
    fill_asks(it->second, trade_price, budget, ev.exchange_ts_ns);
  }
}

}  // namespace mf::phase4
