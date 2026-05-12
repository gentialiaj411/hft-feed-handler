#include <cassert>
#include <cstdint>

#include "mf/core/types.hpp"
#include "mf/phase2/sequence_tracker.hpp"

namespace {

void test_in_order_release() {
  mf::phase2::SequenceTracker tracker(4);
  const auto a = tracker.on_sequence(1);
  assert(a.status == mf::phase2::SequenceStatus::InOrder);
  assert(a.released_count == 1);
  assert(a.expected_after == 2);

  const auto b = tracker.on_sequence(2);
  assert(b.status == mf::phase2::SequenceStatus::InOrder);
  assert(b.released_count == 1);
  assert(b.expected_after == 3);
}

void test_gap_buffer_then_fill() {
  mf::phase2::SequenceTracker tracker(8);

  const auto a = tracker.on_sequence(1);
  assert(a.status == mf::phase2::SequenceStatus::InOrder);
  assert(a.expected_after == 2);

  const auto b = tracker.on_sequence(4);
  assert(b.status == mf::phase2::SequenceStatus::GapBuffered);
  assert(b.missing_range.has_value());
  assert(b.missing_range->begin == 2);
  assert(b.missing_range->end == 3);
  assert(b.expected_after == 2);

  const auto c = tracker.on_sequence(2);
  assert(c.status == mf::phase2::SequenceStatus::InOrder);
  assert(c.released_count == 1);
  assert(c.expected_after == 3);

  const auto d = tracker.on_sequence(3);
  assert(d.status == mf::phase2::SequenceStatus::InOrder);
  // Sequence 4 is already buffered, so it is released in the same step.
  assert(d.released_count == 2);
  assert(d.expected_after == 5);
}

void test_duplicate_and_too_far() {
  mf::phase2::SequenceTracker tracker(2);
  (void)tracker.on_sequence(1);

  const auto dup = tracker.on_sequence(1);
  assert(dup.status == mf::phase2::SequenceStatus::DuplicateOrOld);

  const auto far = tracker.on_sequence(5);
  assert(far.status == mf::phase2::SequenceStatus::GapTooLarge);
  assert(far.missing_range.has_value());
  assert(far.missing_range->begin == 2);
  assert(far.missing_range->end == 4);
  assert(tracker.next_expected() == 2);
}

void test_force_advance_recovers_after_gap_too_large() {
  mf::phase2::SequenceTracker tracker(2);
  (void)tracker.on_sequence(1);
  const auto far = tracker.on_sequence(5);
  assert(far.status == mf::phase2::SequenceStatus::GapTooLarge);
  tracker.force_advance(5);

  const auto recovered = tracker.on_sequence(5);
  assert(recovered.status == mf::phase2::SequenceStatus::InOrder);
  assert(tracker.next_expected() == 6);
}

void test_per_venue_independent_state() {
  mf::phase2::MultiVenueSequenceTracker multi(4);

  const auto n1 = multi.on_sequence(mf::core::Venue::Nasdaq, 1);
  const auto i1 = multi.on_sequence(mf::core::Venue::Iex, 1);
  const auto n2 = multi.on_sequence(mf::core::Venue::Nasdaq, 2);

  assert(n1.status == mf::phase2::SequenceStatus::InOrder);
  assert(i1.status == mf::phase2::SequenceStatus::InOrder);
  assert(n2.status == mf::phase2::SequenceStatus::InOrder);
  assert(multi.tracker_for(mf::core::Venue::Nasdaq).next_expected() == 3);
  assert(multi.tracker_for(mf::core::Venue::Iex).next_expected() == 2);
}

}  // namespace

int main() {
  test_in_order_release();
  test_gap_buffer_then_fill();
  test_duplicate_and_too_far();
  test_force_advance_recovers_after_gap_too_large();
  test_per_venue_independent_state();
  return 0;
}
