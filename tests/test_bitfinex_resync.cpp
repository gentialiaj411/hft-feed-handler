#include <cassert>
#include <string>
#include <vector>

#include "mf/live/bitfinex/resync_state_machine.hpp"
#include "mf/live/bitfinex/wire_types.hpp"

namespace {

void test_cold_start_gapped_until_snapshot() {
  std::vector<mf::core::BookEvent> emitted{};
  mf::live::bitfinex::ResyncStateMachine sm("tBTCUSD", [&](const mf::core::BookEvent& ev) { emitted.push_back(ev); });
  assert(sm.state() == mf::live::bitfinex::FeedState::Gapped);

  mf::live::bitfinex::SnapshotFrame snap{};
  snap.sequence = 10;
  snap.rows.push_back({1001, 50000.0, 0.01});
  const auto action = sm.on_snapshot(snap, 1000, "/tmp/test_sidecar.json.snapshot.json");
  assert(action == mf::live::bitfinex::FeedAction::None);
  assert(sm.state() == mf::live::bitfinex::FeedState::Live);
  assert(emitted.size() == 1);
}

void test_post_snapshot_requires_next_sequence() {
  mf::live::bitfinex::ResyncStateMachine sm("tBTCUSD", [](const mf::core::BookEvent&) {});

  mf::live::bitfinex::SnapshotFrame snap{};
  snap.sequence = 10;
  snap.rows.push_back({1001, 50000.0, 0.01});
  (void)sm.on_snapshot(snap, 1000, "/tmp/test_sidecar_seq.json.snapshot.json");

  mf::live::bitfinex::UpdateFrame ok{};
  ok.row = {1002, 50001.0, -0.02};
  ok.sequence = 11;
  assert(sm.on_update(ok, 2000) == mf::live::bitfinex::FeedAction::None);

  mf::live::bitfinex::ResyncStateMachine sm2("tBTCUSD", [](const mf::core::BookEvent&) {});
  (void)sm2.on_snapshot(snap, 1000, "/tmp/test_sidecar_seq2.json.snapshot.json");

  mf::live::bitfinex::UpdateFrame gap{};
  gap.row = {1003, 50002.0, 0.03};
  gap.sequence = 12;
  const auto action = sm2.on_update(gap, 3000);
  assert(action == mf::live::bitfinex::FeedAction::RequestResubscribe);
  assert(sm2.state() == mf::live::bitfinex::FeedState::Gapped);
  assert(sm2.stats().gaps == 1);
}

void test_gap_triggers_resubscribe() {
  std::vector<mf::core::BookEvent> emitted{};
  mf::live::bitfinex::ResyncStateMachine sm("tBTCUSD", [&](const mf::core::BookEvent& ev) { emitted.push_back(ev); });

  mf::live::bitfinex::SnapshotFrame snap{};
  snap.sequence = 10;
  snap.rows.push_back({1001, 50000.0, 0.01});
  (void)sm.on_snapshot(snap, 1000, "/tmp/test_sidecar2.json.snapshot.json");

  mf::live::bitfinex::UpdateFrame u1{};
  u1.row = {1002, 50001.0, -0.02};
  u1.sequence = 11;
  assert(sm.on_update(u1, 2000) == mf::live::bitfinex::FeedAction::None);

  mf::live::bitfinex::UpdateFrame u_gap{};
  u_gap.row = {1003, 50002.0, 0.03};
  u_gap.sequence = 13;
  const auto action = sm.on_update(u_gap, 3000);
  assert(action == mf::live::bitfinex::FeedAction::RequestResubscribe);
  assert(sm.state() == mf::live::bitfinex::FeedState::Gapped);
  assert(sm.stats().gaps == 1);
}

void test_removal_evicts_on_book() {
  mf::live::bitfinex::ResyncStateMachine sm("tBTCUSD", [](const mf::core::BookEvent&) {});
  mf::live::bitfinex::SnapshotFrame snap{};
  snap.sequence = 100;
  snap.rows.push_back({42, 100.0, 1.0});
  (void)sm.on_snapshot(snap, 1, "/tmp/test_sidecar3.json.snapshot.json");
  assert(sm.on_book().contains(42));

  mf::live::bitfinex::UpdateFrame rem{};
  rem.row = {42, 0.0, 1.0};
  rem.sequence = 101;
  (void)sm.on_update(rem, 2);
  assert(!sm.on_book().contains(42));
}

}  // namespace

int main() {
  test_cold_start_gapped_until_snapshot();
  test_post_snapshot_requires_next_sequence();
  test_gap_triggers_resubscribe();
  test_removal_evicts_on_book();
  return 0;
}
