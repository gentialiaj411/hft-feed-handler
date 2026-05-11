#include "mf/phase2/sequence_tracker.hpp"

#include <algorithm>
#include <array>

namespace mf::phase2 {

namespace {
constexpr std::uint64_t kMinRingSlots = 2;
constexpr std::array<mf::core::Venue, 3> kTrackedVenues = {
    mf::core::Venue::Nasdaq,
    mf::core::Venue::Iex,
    mf::core::Venue::Cboe,
};
}  // namespace

SequenceTracker::SequenceTracker(std::uint64_t gap_window)
    : gap_window_(gap_window),
      buffered_tags_(static_cast<std::size_t>(std::max(kMinRingSlots, gap_window + 1U)), 0) {}

void SequenceTracker::mark_buffered(std::uint64_t seq) noexcept {
  const std::size_t slot = static_cast<std::size_t>(seq % buffered_tags_.size());
  buffered_tags_[slot] = seq;
}

bool SequenceTracker::pop_buffered(std::uint64_t seq) noexcept {
  const std::size_t slot = static_cast<std::size_t>(seq % buffered_tags_.size());
  if (buffered_tags_[slot] != seq) {
    return false;
  }
  buffered_tags_[slot] = 0;
  return true;
}

SequenceUpdate SequenceTracker::on_sequence(std::uint64_t seq) noexcept {
  SequenceUpdate out{};
  out.input_sequence = seq;
  out.expected_before = next_expected_;
  out.expected_after = next_expected_;

  if (seq == 0) {
    out.status = SequenceStatus::DuplicateOrOld;
    return out;
  }

  if (seq < next_expected_) {
    out.status = SequenceStatus::DuplicateOrOld;
    return out;
  }

  if (seq == next_expected_) {
    out.status = SequenceStatus::InOrder;
    ++next_expected_;
    ++out.released_count;

    while (pop_buffered(next_expected_)) {
      ++next_expected_;
      ++out.released_count;
    }

    out.expected_after = next_expected_;
    return out;
  }

  const std::uint64_t ahead = seq - next_expected_;
  if (ahead > gap_window_) {
    out.status = SequenceStatus::GapTooLarge;
    out.missing_range = GapRange{next_expected_, seq - 1U};
    return out;
  }

  out.status = SequenceStatus::GapBuffered;
  out.missing_range = GapRange{next_expected_, seq - 1U};
  mark_buffered(seq);
  return out;
}

MultiVenueSequenceTracker::MultiVenueSequenceTracker(std::uint64_t gap_window) {
  trackers_.reserve(kTrackedVenues.size());
  for (std::size_t i = 0; i < kTrackedVenues.size(); ++i) {
    trackers_.emplace_back(gap_window);
  }
}

std::size_t MultiVenueSequenceTracker::index_for(mf::core::Venue venue) const noexcept {
  for (std::size_t i = 0; i < kTrackedVenues.size(); ++i) {
    if (kTrackedVenues[i] == venue) {
      return i;
    }
  }
  return 0;
}

SequenceUpdate MultiVenueSequenceTracker::on_sequence(mf::core::Venue venue, std::uint64_t seq) noexcept {
  return trackers_[index_for(venue)].on_sequence(seq);
}

const SequenceTracker& MultiVenueSequenceTracker::tracker_for(mf::core::Venue venue) const noexcept {
  return trackers_[index_for(venue)];
}

}  // namespace mf::phase2
