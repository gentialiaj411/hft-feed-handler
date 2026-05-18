#include <cassert>
#include <cstdio>
#include <vector>

#include "mf/research/event_store.hpp"
#include "mf/research/experiment_runner.hpp"

#if defined(__linux__)
#include <unistd.h>
#endif

namespace {
mf::core::BookEvent ev(
    mf::core::EventType type,
    mf::core::Side side,
    std::uint32_t price,
    std::uint32_t qty,
    std::uint64_t ts,
    std::uint64_t order_id = 0) {
  mf::core::BookEvent e{};
  e.venue = mf::core::Venue::Nasdaq;
  e.type = type;
  e.side = side;
  e.price = price;
  e.qty = qty;
  e.exchange_ts_ns = ts;
  e.ingest_ts_ns = ts;
  e.order_id = order_id;
  e.symbol.bytes = {'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '};
  return e;
}
}  // namespace

int main() {
#if !defined(__linux__)
  std::puts("SKIP: experiment runner test is Linux-only");
  return 0;
#else
  char path[] = "/tmp/mf_experiment_XXXXXX";
  const int fd = ::mkstemp(path);
  assert(fd >= 0);
  ::close(fd);
  ::unlink(path);

  std::vector<mf::core::BookEvent> tape;
  tape.push_back(ev(mf::core::EventType::Add, mf::core::Side::Buy, 100, 200, 1, 1));
  tape.push_back(ev(mf::core::EventType::Add, mf::core::Side::Sell, 102, 200, 2, 2));
  tape.push_back(ev(mf::core::EventType::Trade, mf::core::Side::Sell, 100, 100, 3));
  tape.push_back(ev(mf::core::EventType::Trade, mf::core::Side::Buy, 102, 100, 4));

  mf::research::EventStore store(path);
  assert(store.append(tape, true));

  mf::research::ExperimentRunner runner;
  mf::research::ExperimentReport a{};
  mf::research::ExperimentReport b{};
  assert(runner.run(store, a));
  assert(runner.run(store, b));
  assert(a.input.records == tape.size());
  assert(a.clock_rejects == 0);
  assert(a.submitted_orders > 0);
  assert(a.fills > 0);
  assert(a.input.crc == b.input.crc);
  assert(a.output_hash == b.output_hash);
  assert(a.config_hash == b.config_hash);
  assert(a.pnl.fills == b.pnl.fills);

  const auto json = mf::research::experiment_report_to_json(a, path);
  assert(json.find("\"config_hash\"") != std::string::npos);
  assert(json.find("\"output_hash\"") != std::string::npos);
  ::unlink(path);
  return 0;
#endif
}
