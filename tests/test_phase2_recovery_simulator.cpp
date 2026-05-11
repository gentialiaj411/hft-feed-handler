#include <cassert>
#include <cstdint>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/phase2/recovery_simulator.hpp"

namespace {

mf::core::BookEvent make_event(mf::core::Venue venue, std::uint64_t seq) {
  mf::core::BookEvent ev{};
  ev.venue = venue;
  ev.sequence = seq;
  ev.exchange_ts_ns = 1000 + seq;
  return ev;
}

void test_recovery_lookup_and_drain() {
  mf::phase2::ReplayRecoveryStore store;
  store.record_event(make_event(mf::core::Venue::Nasdaq, 1));
  store.record_event(make_event(mf::core::Venue::Nasdaq, 3));
  store.record_event(make_event(mf::core::Venue::Nasdaq, 4));

  mf::phase2::ReplayRecoverySimulator sim(&store);
  mf::phase2::RecoveryRequest req{};
  req.venue = mf::core::Venue::Nasdaq;
  req.missing.begin = 2;
  req.missing.end = 4;
  req.reason = mf::phase2::RecoveryReason::GapDetected;
  sim.request_recovery(req);

  std::vector<mf::core::BookEvent> recovered = sim.drain_recovered();
  assert(sim.requests_total() == 1);
  assert(sim.recovered_events_total() == 2);
  assert(recovered.size() == 2);
  assert(recovered[0].sequence == 3);
  assert(recovered[1].sequence == 4);

  auto recovered_again = sim.drain_recovered();
  assert(recovered_again.empty());
}

}  // namespace

int main() {
  test_recovery_lookup_and_drain();
  return 0;
}
