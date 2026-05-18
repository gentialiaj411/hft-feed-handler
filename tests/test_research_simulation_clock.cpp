#include <cassert>

#include "mf/core/types.hpp"
#include "mf/research/simulation_clock.hpp"

int main() {
  mf::research::SimulationClock clock;
  assert(clock.now_ns() == 0);
  assert(clock.events_seen() == 0);
  assert(clock.advance_to(100));
  assert(clock.now_ns() == 100);
  assert(clock.events_seen() == 1);
  assert(!clock.advance_to(99));
  assert(clock.now_ns() == 100);
  assert(clock.events_seen() == 1);

  mf::core::BookEvent ev{};
  ev.exchange_ts_ns = 150;
  assert(clock.on_event(ev));
  assert(clock.now_ns() == 150);
  assert(clock.events_seen() == 2);

  clock.reset(7);
  assert(clock.now_ns() == 7);
  assert(clock.events_seen() == 0);
  return 0;
}
