#include <cassert>
#include <fstream>
#include <sstream>
#include <string>

#include "mf/core/types.hpp"
#include "mf/live/bitfinex/lowering.hpp"
#include "mf/live/bitfinex/wire_types.hpp"

namespace {

std::string read_fixture(const char* name) {
  std::ifstream in(std::string("tests/fixtures/bitfinex/") + name);
  std::ostringstream os;
  os << in.rdbuf();
  return os.str();
}

void test_snapshot_adds() {
  mf::live::bitfinex::ParsedFrame frame{};
  assert(mf::live::bitfinex::parse_frame(read_fixture("snapshot.json"), frame));
  assert(frame.kind == mf::live::bitfinex::ParsedKind::Snapshot);
  assert(frame.snapshot.rows.size() == 2);
  assert(frame.snapshot.sequence == 1);

  mf::live::bitfinex::OnBookTracker on_book{};
  mf::live::bitfinex::LoweringStats stats{};
  const auto events = mf::live::bitfinex::lower_snapshot_rows(frame.snapshot.rows, 100, "tBTCUSD", on_book, stats);
  assert(events.size() == 2);
  assert(events[0].type == mf::core::EventType::Add);
  assert(events[0].order_id == 238803645123ULL);
  assert(events[0].side == mf::core::Side::Buy);
  assert(events[1].side == mf::core::Side::Sell);
  assert(on_book.size() == 2);
  assert(stats.adds == 2);
}

void test_update_add_and_remove() {
  mf::live::bitfinex::OnBookTracker on_book{};
  mf::live::bitfinex::LoweringStats stats{};

  mf::live::bitfinex::ParsedFrame snap{};
  assert(mf::live::bitfinex::parse_frame(read_fixture("snapshot.json"), snap));
  (void)mf::live::bitfinex::lower_snapshot_rows(snap.snapshot.rows, 100, "tBTCUSD", on_book, stats);

  mf::live::bitfinex::ParsedFrame add{};
  assert(mf::live::bitfinex::parse_frame(read_fixture("add_update.json"), add));
  const auto add_ev = mf::live::bitfinex::lower_row(add.update.row, add.update.sequence, 200, "tBTCUSD", on_book, stats);
  assert(add_ev.has_value());
  assert(add_ev->type == mf::core::EventType::Add);
  assert(on_book.contains(238803645125ULL));

  mf::live::bitfinex::ParsedFrame rem{};
  assert(mf::live::bitfinex::parse_frame(read_fixture("remove_bid.json"), rem));
  const auto cancel_ev =
      mf::live::bitfinex::lower_row(rem.update.row, rem.update.sequence, 300, "tBTCUSD", on_book, stats);
  assert(cancel_ev.has_value());
  assert(cancel_ev->type == mf::core::EventType::Cancel);
  assert(!on_book.contains(238803645123ULL));
  assert(stats.cancels == 1);
}

void test_no_execute_event_type() {
  mf::live::bitfinex::ParsedFrame rem{};
  assert(mf::live::bitfinex::parse_frame(read_fixture("remove_bid.json"), rem));
  mf::live::bitfinex::OnBookTracker on_book{};
  on_book.insert(238803645123ULL, 950005000U, 10000U);
  mf::live::bitfinex::LoweringStats stats{};
  const auto ev = mf::live::bitfinex::lower_row(rem.update.row, 3, 1, "tBTCUSD", on_book, stats);
  assert(ev.has_value());
  assert(ev->type != mf::core::EventType::Execute);
  assert(ev->type == mf::core::EventType::Cancel);
}

}  // namespace

int main() {
  test_snapshot_adds();
  test_update_add_and_remove();
  test_no_execute_event_type();
  return 0;
}
