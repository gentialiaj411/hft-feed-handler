#include "mf/phase3/nbbo_consolidator.hpp"

#include <cassert>

namespace mf::phase3 {

std::size_t NbboConsolidator::venue_index(mf::core::Venue venue) noexcept {
  const std::size_t idx = static_cast<std::size_t>(static_cast<std::uint8_t>(venue));
  assert(idx < 3);
  return idx;
}

Nbbo NbboConsolidator::compute(const SymbolState& state) noexcept {
  Nbbo out{};
  for (std::size_t i = 0; i < state.venues.size(); ++i) {
    const auto& v = state.venues[i];
    if (v.has_bid) {
      if (!out.has_bid || v.bid_price > out.bid_price) {
        out.has_bid = true;
        out.bid_price = v.bid_price;
        out.bid_qty = v.bid_qty;
        out.bid_venue = static_cast<std::uint8_t>(i);
      }
    }
    if (v.has_ask) {
      if (!out.has_ask || v.ask_price < out.ask_price) {
        out.has_ask = true;
        out.ask_price = v.ask_price;
        out.ask_qty = v.ask_qty;
        out.ask_venue = static_cast<std::uint8_t>(i);
      }
    }
  }
  return out;
}

bool NbboConsolidator::update(std::uint64_t symbol_u64, mf::core::Venue venue, const TopOfBook& top) noexcept {
  auto& s = by_symbol_[symbol_u64];
  auto& v = s.venues[venue_index(venue)];
  v.has_bid = top.has_bid;
  v.has_ask = top.has_ask;
  v.bid_price = top.bid_price;
  v.bid_qty = top.bid_qty;
  v.ask_price = top.ask_price;
  v.ask_qty = top.ask_qty;

  const Nbbo before = s.nbbo;
  s.nbbo = compute(s);
  return (
      before.has_bid != s.nbbo.has_bid ||
      before.has_ask != s.nbbo.has_ask ||
      before.bid_price != s.nbbo.bid_price ||
      before.ask_price != s.nbbo.ask_price ||
      before.bid_qty != s.nbbo.bid_qty ||
      before.ask_qty != s.nbbo.ask_qty ||
      before.bid_venue != s.nbbo.bid_venue ||
      before.ask_venue != s.nbbo.ask_venue);
}

Nbbo NbboConsolidator::current(std::uint64_t symbol_u64) const noexcept {
  auto it = by_symbol_.find(symbol_u64);
  if (it == by_symbol_.end()) {
    return {};
  }
  return it->second.nbbo;
}

}  // namespace mf::phase3
