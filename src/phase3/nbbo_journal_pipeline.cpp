#include "mf/phase3/nbbo_journal_pipeline.hpp"

#include "mf/core/crc32.hpp"
#include "mf/journal/nbbo_event.hpp"
#include "mf/journal/nbbo_journal_writer.hpp"
#include "mf/phase3/nbbo_consolidator.hpp"
#include "mf/phase3/order_book_engine.hpp"

namespace mf::phase3 {

namespace {
mf::journal::NbboEvent to_nbbo_event(
    std::uint64_t symbol_u64,
    const Nbbo& nbbo,
    std::uint64_t exchange_ts_ns,
    std::uint64_t ingest_ts_ns) noexcept {
  mf::journal::NbboEvent out{};
  out.symbol_u64 = symbol_u64;
  out.exchange_ts_ns = exchange_ts_ns;
  out.ingest_ts_ns = ingest_ts_ns;
  out.has_bid = nbbo.has_bid;
  out.has_ask = nbbo.has_ask;
  out.bid_price = nbbo.bid_price;
  out.bid_qty = nbbo.bid_qty;
  out.ask_price = nbbo.ask_price;
  out.ask_qty = nbbo.ask_qty;
  out.bid_venue = nbbo.bid_venue;
  out.ask_venue = nbbo.ask_venue;
  out.bid_venue_sequence = nbbo.bid_venue_sequence;
  out.ask_venue_sequence = nbbo.ask_venue_sequence;
  return out;
}
}  // namespace

bool NbboJournalPipeline::replay_to_journal(
    const std::span<const mf::core::BookEvent> events,
    const std::string& out_path,
    NbboJournalPipelineStats& stats) {
  stats = {};
  mf::journal::NbboJournalWriter writer(1U << 20U, false);
  if (!writer.open(out_path)) {
    return false;
  }

  OrderBookEngine books;
  NbboConsolidator nbbo;

  for (const auto& ev : events) {
    ++stats.book_events_in;
    const auto apply = books.on_event(ev);
    if (!apply.changed_top) {
      continue;
    }
    ++stats.top_changes;
    const std::uint64_t symbol = ev.symbol.as_u64();
    if (!nbbo.update(symbol, ev.venue, apply.top_after, ev.sequence)) {
      continue;
    }
    const auto after = nbbo.current(symbol);
    const auto record = to_nbbo_event(symbol, after, ev.exchange_ts_ns, ev.ingest_ts_ns);
    writer.append(record);
    stats.journal_crc = mf::core::crc32_update(
        stats.journal_crc,
        reinterpret_cast<const std::byte*>(&record),
        sizeof(record));
    ++stats.nbbo_emitted;
  }

  writer.close();
  return true;
}

}  // namespace mf::phase3
