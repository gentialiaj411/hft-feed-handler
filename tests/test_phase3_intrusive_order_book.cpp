#include <cassert>
#include <cstdint>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/phase3/book_snapshot.hpp"
#include "mf/phase3/intrusive_order_book.hpp"
#include "mf/phase3/order_book_engine.hpp"

namespace {

mf::core::SymbolKey symbol_for(std::uint64_t idx) {
  mf::core::SymbolKey s{};
  const char suffix = static_cast<char>('1' + static_cast<char>(idx % 4U));
  s.bytes = {'S', 'Y', 'M', 'B', '0', '0', '0', suffix};
  return s;
}

std::vector<mf::core::BookEvent> make_events(std::uint64_t cycles) {
  std::vector<mf::core::BookEvent> events;
  events.reserve(static_cast<std::size_t>(cycles * 4U * 5U));
  std::uint64_t seq = 1;
  for (std::uint64_t cycle = 0; cycle < cycles; ++cycle) {
    for (std::uint64_t sym = 0; sym < 4; ++sym) {
      const std::uint64_t base = 1000000ULL + cycle * 100ULL + sym * 10ULL;
      const std::uint32_t price = 10000U + static_cast<std::uint32_t>((cycle + sym) % 32U);

      mf::core::BookEvent add_bid{};
      add_bid.venue = static_cast<mf::core::Venue>(sym % 3U);
      add_bid.type = mf::core::EventType::Add;
      add_bid.sequence = seq++;
      add_bid.exchange_ts_ns = add_bid.sequence;
      add_bid.symbol = symbol_for(sym);
      add_bid.side = mf::core::Side::Buy;
      add_bid.price = price;
      add_bid.qty = 100;
      add_bid.order_id = base + 1U;
      events.push_back(add_bid);

      auto add_ask = add_bid;
      add_ask.sequence = seq++;
      add_ask.exchange_ts_ns = add_ask.sequence;
      add_ask.side = mf::core::Side::Sell;
      add_ask.price = price + 20U;
      add_ask.qty = 120;
      add_ask.order_id = base + 2U;
      events.push_back(add_ask);

      auto exec_bid = add_bid;
      exec_bid.type = mf::core::EventType::Execute;
      exec_bid.sequence = seq++;
      exec_bid.exchange_ts_ns = exec_bid.sequence;
      exec_bid.side = mf::core::Side::Unknown;
      exec_bid.price = 0;
      exec_bid.qty = 40;
      exec_bid.order_id = base + 1U;
      events.push_back(exec_bid);

      auto cancel_ask = add_ask;
      cancel_ask.type = mf::core::EventType::Cancel;
      cancel_ask.sequence = seq++;
      cancel_ask.exchange_ts_ns = cancel_ask.sequence;
      cancel_ask.side = mf::core::Side::Unknown;
      cancel_ask.price = 0;
      cancel_ask.qty = 30;
      cancel_ask.order_id = base + 2U;
      events.push_back(cancel_ask);

      auto replace_bid = add_bid;
      replace_bid.type = mf::core::EventType::Replace;
      replace_bid.sequence = seq++;
      replace_bid.exchange_ts_ns = replace_bid.sequence;
      replace_bid.side = mf::core::Side::Buy;
      replace_bid.price = price + 1U;
      replace_bid.qty = 50;
      replace_bid.order_id = base + 3U;
      replace_bid.reference_order_id = base + 1U;
      events.push_back(replace_bid);
    }
  }
  return events;
}

void test_intrusive_matches_reference_snapshots() {
  mf::phase3::OrderBookEngine reference;
  mf::phase3::IntrusiveOrderBook intrusive;
  const auto events = make_events(100);

  for (const auto& ev : events) {
    const auto ref = reference.on_event(ev);
    const auto fast = intrusive.on_event(ev);
    assert(ref.changed_top == fast.changed_top);
  }

  for (std::uint64_t sym = 0; sym < 4; ++sym) {
    const auto venue = static_cast<mf::core::Venue>(sym % 3U);
    const auto symbol = symbol_for(sym).as_u64();
    assert(byte_equal(reference.snapshot(venue, symbol), intrusive.snapshot(venue, symbol)));
    const auto ref_top = reference.top_of_book(venue, symbol);
    const auto fast_top = intrusive.top_of_book(venue, symbol);
    assert(ref_top.has_bid == fast_top.has_bid);
    assert(ref_top.has_ask == fast_top.has_ask);
    assert(ref_top.bid_price == fast_top.bid_price);
    assert(ref_top.bid_qty == fast_top.bid_qty);
    assert(ref_top.ask_price == fast_top.ask_price);
    assert(ref_top.ask_qty == fast_top.ask_qty);
  }
  assert(intrusive.dropped_order_inserts() == 0);
}

void test_pool_exhaustion_is_reported() {
  mf::phase3::IntrusiveOrderBook book(/*order_capacity=*/2);
  auto ev = make_events(1).front();
  assert(!book.on_event(ev).order_pool_exhausted);
  ev.order_id += 100;
  assert(!book.on_event(ev).order_pool_exhausted);
  ev.order_id += 100;
  assert(book.on_event(ev).order_pool_exhausted);
  assert(book.dropped_order_inserts() == 1);
}

}  // namespace

int main() {
  test_intrusive_matches_reference_snapshots();
  test_pool_exhaustion_is_reported();
  return 0;
}
