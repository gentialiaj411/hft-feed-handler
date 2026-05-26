#include <cassert>

#include "mf/research/signals/ofi.hpp"

namespace {
mf::core::BookEvent mk(mf::core::Side side, std::uint32_t px, std::uint32_t qty, std::uint64_t ts) {
  mf::core::BookEvent e{};
  e.side = side;
  e.price = px;
  e.qty = qty;
  e.exchange_ts_ns = ts;
  return e;
}
}  // namespace

int main() {
  mf::research::OfiSignal s({.max_events = 64, .max_window_ns = 0});

  s.update(mk(mf::core::Side::Buy, 100, 10, 1));   // +10
  s.update(mk(mf::core::Side::Buy, 100, 12, 2));   // +2
  s.update(mk(mf::core::Side::Buy, 99, 8, 3));     // -12
  s.update(mk(mf::core::Side::Sell, 101, 9, 4));   // -9
  s.update(mk(mf::core::Side::Sell, 101, 7, 5));   // +2
  s.update(mk(mf::core::Side::Sell, 102, 5, 6));   // -5

  const double expected = -12.0;
  assert(s.value() == expected);
  return 0;
}
