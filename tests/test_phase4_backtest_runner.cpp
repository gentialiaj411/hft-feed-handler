#include <cassert>
#include <cmath>
#include <vector>

#include "mf/phase4/backtest_runner.hpp"

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
  std::vector<mf::core::BookEvent> tape;
  tape.push_back(ev(mf::core::EventType::Add, mf::core::Side::Buy, 100, 200, 1, 1));
  tape.push_back(ev(mf::core::EventType::Add, mf::core::Side::Sell, 102, 200, 2, 2));
  tape.push_back(ev(mf::core::EventType::Trade, mf::core::Side::Sell, 100, 100, 3));
  tape.push_back(ev(mf::core::EventType::Trade, mf::core::Side::Buy, 102, 100, 4));

  mf::phase4::BacktestRunner runner;
  const auto rep = runner.run(tape);
  assert(rep.fills > 0);
  assert(std::isfinite(rep.total_pnl));
  return 0;
}
