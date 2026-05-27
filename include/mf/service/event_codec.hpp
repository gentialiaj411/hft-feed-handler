#pragma once

#include "mf/core/types.hpp"
#include "mf/journal/nbbo_event.hpp"
#include "mf/service/symbol_codec.hpp"
#include "tick_service.pb.h"

namespace mf::service {

inline void book_event_to_proto(const mf::core::BookEvent& ev, mf::replay::v1::BookEventMsg& out) {
  out.set_venue(static_cast<std::uint32_t>(ev.venue));
  out.set_event_type(static_cast<std::uint32_t>(ev.type));
  out.set_sequence(ev.sequence);
  out.set_exchange_ts_ns(ev.exchange_ts_ns);
  out.set_ingest_ts_ns(ev.ingest_ts_ns);
  out.set_symbol(symbol_to_string(ev.symbol.as_u64()));
  out.set_order_id(ev.order_id);
  out.set_match_id(ev.match_id);
  out.set_reference_order_id(ev.reference_order_id);
  out.set_qty(ev.qty);
  out.set_price(ev.price);
  out.set_prev_qty(ev.prev_qty);
  out.set_prev_price(ev.prev_price);
  out.set_side(static_cast<std::uint32_t>(ev.side));
  out.set_raw_type(ev.raw_type);
  if (ev.mpid.has_value()) {
    out.set_has_mpid(true);
    out.set_mpid(std::string(ev.mpid->data(), ev.mpid->size()));
  } else {
    out.set_has_mpid(false);
  }
}

inline void nbbo_event_to_proto(const mf::journal::NbboEvent& ev, mf::replay::v1::NbboEventMsg& out) {
  out.set_symbol(symbol_to_string(ev.symbol_u64));
  out.set_exchange_ts_ns(ev.exchange_ts_ns);
  out.set_ingest_ts_ns(ev.ingest_ts_ns);
  out.set_has_bid(ev.has_bid);
  out.set_has_ask(ev.has_ask);
  out.set_bid_price(ev.bid_price);
  out.set_bid_qty(ev.bid_qty);
  out.set_ask_price(ev.ask_price);
  out.set_ask_qty(ev.ask_qty);
  out.set_bid_venue(ev.bid_venue);
  out.set_ask_venue(ev.ask_venue);
  out.set_bid_venue_sequence(ev.bid_venue_sequence);
  out.set_ask_venue_sequence(ev.ask_venue_sequence);
}

inline bool in_ts_window(std::uint64_t ts, std::uint64_t start_ts, std::uint64_t end_ts) noexcept {
  if (start_ts > 0 && ts < start_ts) {
    return false;
  }
  if (end_ts > 0 && ts > end_ts) {
    return false;
  }
  return true;
}

}  // namespace mf::service
