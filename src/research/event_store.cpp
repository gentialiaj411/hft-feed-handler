#include "mf/research/event_store.hpp"

#include <utility>

#include "mf/core/book_event_crc.hpp"
#include "mf/journal/journal_reader.hpp"
#include "mf/journal/journal_writer.hpp"

namespace mf::research {

namespace {
void update_stats(EventStoreStats& stats, const mf::core::BookEvent& ev) {
  mf::core::update_crc_from_book_event(stats.crc, ev);
  if (stats.records == 0) {
    stats.first_exchange_ts_ns = ev.exchange_ts_ns;
  }
  stats.last_exchange_ts_ns = ev.exchange_ts_ns;
  ++stats.records;
}
}  // namespace

EventStore::EventStore(std::string path) : path_(std::move(path)) {}

bool EventStore::append(std::span<const mf::core::BookEvent> events, bool fsync_on_close) const {
  mf::journal::JournalWriter writer(1U << 20U, fsync_on_close);
  if (!writer.open(path_)) {
    return false;
  }
  for (const auto& ev : events) {
    writer.append(ev, ev.ingest_ts_ns);
  }
  writer.close();
  return true;
}

bool EventStore::replay(IEventConsumer& consumer, EventStoreStats* stats) const {
  mf::journal::JournalReader reader;
  if (!reader.open(path_)) {
    return false;
  }

  EventStoreStats local{};
  mf::core::BookEvent ev{};
  std::uint64_t ingest_ts_ns = 0;
  std::uint64_t seq = 0;
  while (reader.next(ev, ingest_ts_ns, seq)) {
    (void)seq;
    ev.ingest_ts_ns = ingest_ts_ns;
    update_stats(local, ev);
    consumer.on_event(ev);
  }
  local.crc_failures = reader.stats().crc_failures;
  if (stats != nullptr) {
    *stats = local;
  }
  return local.crc_failures == 0;
}

std::vector<mf::core::BookEvent> EventStore::load_all(EventStoreStats* stats) const {
  struct Collector final : IEventConsumer {
    std::vector<mf::core::BookEvent> events{};
    void on_event(const mf::core::BookEvent& ev) override { events.push_back(ev); }
  };

  Collector collector{};
  (void)replay(collector, stats);
  return collector.events;
}

}  // namespace mf::research
