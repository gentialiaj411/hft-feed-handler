#include <cassert>
#include <cstdio>
#include <vector>

#include "mf/research/event_store.hpp"

#if defined(__linux__)
#include <unistd.h>
#endif

namespace {
mf::core::BookEvent make_event(std::uint64_t seq) {
  mf::core::BookEvent ev{};
  ev.venue = mf::core::Venue::Nasdaq;
  ev.type = mf::core::EventType::Add;
  ev.sequence = seq;
  ev.exchange_ts_ns = 1000 + seq;
  ev.ingest_ts_ns = 2000 + seq;
  ev.symbol.bytes = {'A', 'A', 'P', 'L', 0, 0, 0, 0};
  ev.order_id = 10 + seq;
  ev.qty = static_cast<std::uint32_t>(100 + seq);
  ev.price = static_cast<std::uint32_t>(10000 + seq);
  ev.side = mf::core::Side::Buy;
  return ev;
}

struct CollectConsumer final : mf::research::IEventConsumer {
  std::vector<mf::core::BookEvent> events{};
  void on_event(const mf::core::BookEvent& ev) override { events.push_back(ev); }
};
}  // namespace

int main() {
#if !defined(__linux__)
  std::puts("SKIP: EventStore journal backend is Linux-only");
  return 0;
#else
  char path[] = "/tmp/mf_research_store_XXXXXX";
  const int fd = ::mkstemp(path);
  assert(fd >= 0);
  ::close(fd);
  ::unlink(path);

  const std::vector<mf::core::BookEvent> input{make_event(1), make_event(2), make_event(3)};
  mf::research::EventStore store(path);
  assert(store.append(input, true));

  CollectConsumer consumer;
  mf::research::EventStoreStats stats{};
  assert(store.replay(consumer, &stats));
  assert(consumer.events.size() == input.size());
  assert(stats.records == input.size());
  assert(stats.crc_failures == 0);
  assert(stats.first_exchange_ts_ns == input.front().exchange_ts_ns);
  assert(stats.last_exchange_ts_ns == input.back().exchange_ts_ns);
  for (std::size_t i = 0; i < input.size(); ++i) {
    assert(consumer.events[i].sequence == input[i].sequence);
    assert(consumer.events[i].ingest_ts_ns == input[i].ingest_ts_ns);
    assert(consumer.events[i].price == input[i].price);
  }

  auto loaded = store.load_all();
  assert(loaded.size() == input.size());
  ::unlink(path);
  return 0;
#endif
}
