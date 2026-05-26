#include "mf/phase3/nbbo_consolidator.hpp"

#include <cassert>

namespace mf::phase3 {

NbboConsolidator::NbboConsolidator() {
  by_symbol_.reserve(1024);
}

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
        out.bid_venue_sequence = v.bid_sequence;
      }
    }
    if (v.has_ask) {
      if (!out.has_ask || v.ask_price < out.ask_price) {
        out.has_ask = true;
        out.ask_price = v.ask_price;
        out.ask_qty = v.ask_qty;
        out.ask_venue = static_cast<std::uint8_t>(i);
        out.ask_venue_sequence = v.ask_sequence;
      }
    }
  }
  return out;
}

namespace {
bool nbbo_changed(const Nbbo& before, const Nbbo& after) noexcept {
  return before.has_bid != after.has_bid || before.has_ask != after.has_ask ||
         before.bid_price != after.bid_price || before.ask_price != after.ask_price ||
         before.bid_qty != after.bid_qty || before.ask_qty != after.ask_qty ||
         before.bid_venue != after.bid_venue || before.ask_venue != after.ask_venue ||
         before.bid_venue_sequence != after.bid_venue_sequence ||
         before.ask_venue_sequence != after.ask_venue_sequence;
}
}  // namespace

bool NbboConsolidator::update(std::uint64_t symbol_u64, mf::core::Venue venue, const TopOfBook& top) noexcept {
  const auto before = current(symbol_u64);
  const auto after = update_and_current(symbol_u64, venue, top);
  return nbbo_changed(before, after);
}

bool NbboConsolidator::update(
    std::uint64_t symbol_u64,
    mf::core::Venue venue,
    const TopOfBook& top,
    const std::uint64_t venue_sequence) noexcept {
  const auto before = current(symbol_u64);
  const auto after = update_and_current(symbol_u64, venue, top, venue_sequence);
  return nbbo_changed(before, after);
}

Nbbo NbboConsolidator::update_and_current(
    std::uint64_t symbol_u64,
    mf::core::Venue venue,
    const TopOfBook& top,
    const std::uint64_t venue_sequence) noexcept {
  SymbolState* state = nullptr;
  if (cached_valid_ && cached_symbol_ == symbol_u64 && cached_state_ != nullptr) {
    state = cached_state_;
  } else {
    auto it = by_symbol_.find(symbol_u64);
    if (it == by_symbol_.end()) {
      cached_valid_ = false;
      it = by_symbol_.emplace(symbol_u64, SymbolState{}).first;
    }
    cached_symbol_ = symbol_u64;
    cached_state_ = &it->second;
    cached_valid_ = true;
    state = &it->second;
  }
  auto& s = *state;
  auto& v = s.venues[venue_index(venue)];
  v.has_bid = top.has_bid;
  v.has_ask = top.has_ask;
  v.bid_price = top.bid_price;
  v.bid_qty = top.bid_qty;
  v.ask_price = top.ask_price;
  v.ask_qty = top.ask_qty;
  if (top.has_bid) {
    v.bid_sequence = venue_sequence;
  }
  if (top.has_ask) {
    v.ask_sequence = venue_sequence;
  }

  s.nbbo = compute(s);
  return s.nbbo;
}

Nbbo NbboConsolidator::update_and_current(std::uint64_t symbol_u64, mf::core::Venue venue, const TopOfBook& top) noexcept {
  return update_and_current(symbol_u64, venue, top, 0);
}

Nbbo NbboConsolidator::current(std::uint64_t symbol_u64) const noexcept {
  auto it = by_symbol_.find(symbol_u64);
  if (it == by_symbol_.end()) {
    return {};
  }
  return it->second.nbbo;
}

}  // namespace mf::phase3
