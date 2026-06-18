#include "mf/phase2/ab_arbiter.hpp"

#include <algorithm>
#include <cassert>

namespace mf::phase2 {

std::size_t AbArbiter::venue_index(mf::core::Venue venue) noexcept {
  const std::size_t idx = static_cast<std::size_t>(static_cast<std::uint8_t>(venue));
  assert(idx < mf::core::kVenueSlotCount);
  return idx;
}

AbArbiterResult AbArbiter::on_event(FeedSide side, const mf::core::BookEvent& ev) noexcept {
  AbArbiterResult out{};
  const auto s = sequencer_.on_sequence(ev.venue, ev.sequence);
  out.status = s.update.status;
  out.recovery = s.recovery;

  if (s.update.status == SequenceStatus::DuplicateOrOld) {
    ++stats_.duplicate_or_old;
    return out;
  }
  if (s.update.status == SequenceStatus::GapBuffered) {
    pending_[venue_index(ev.venue)].push_back(ev);
    ++stats_.gap_buffered;
    return out;
  }
  if (s.update.status == SequenceStatus::GapTooLarge) {
    ++stats_.gap_too_large;
    sequencer_.force_advance(ev.venue, ev.sequence);
    ++stats_.forced_advances;
    return out;
  }

  // InOrder path.
  out.accepted = true;
  ready_.push_back(ev);
  ++stats_.accepted;
  if (side == FeedSide::A) {
    ++stats_.accepted_a;
  } else {
    ++stats_.accepted_b;
  }

  auto& pend = pending_[venue_index(ev.venue)];
  if (!pend.empty()) {
    std::sort(pend.begin(), pend.end(), [](const mf::core::BookEvent& a, const mf::core::BookEvent& b) {
      return a.sequence < b.sequence;
    });
    const std::uint64_t next = sequencer_.next_expected(ev.venue);
    auto it = pend.begin();
    while (it != pend.end()) {
      if (it->sequence < next) {
        it = pend.erase(it);
        continue;
      }
      if (it->sequence == next) {
        // Re-run as recovered/incoming path.
        const mf::core::BookEvent replay = *it;
        it = pend.erase(it);
        (void)on_event(side, replay);
        continue;
      }
      ++it;
    }
  }
  return out;
}

std::vector<mf::core::BookEvent> AbArbiter::drain_ready() noexcept {
  std::vector<mf::core::BookEvent> out;
  out.swap(ready_);
  return out;
}

std::vector<std::pair<FeedSide, mf::core::BookEvent>> make_dual_feed_race_stream(
    const std::vector<mf::core::BookEvent>& source,
    const DualFeedDropConfig& cfg,
    DroppedFeedCounts* counts) {
  std::mt19937_64 rng(cfg.seed);
  std::uniform_real_distribution<double> dist(0.0, 1.0);

  std::vector<std::pair<FeedSide, mf::core::BookEvent>> out;
  out.reserve(source.size() * 2U);

  DroppedFeedCounts local{};
  for (const auto& ev : source) {
    const bool keep_a = dist(rng) >= cfg.drop_rate_a;
    const bool keep_b = dist(rng) >= cfg.drop_rate_b;
    if (!keep_a) {
      ++local.dropped_a;
    }
    if (!keep_b) {
      ++local.dropped_b;
    }
    if (!keep_a && !keep_b) {
      continue;
    }

    // Deterministic race ordering derived from sequence parity.
    const bool a_first = ((ev.sequence & 1ULL) == 0ULL);
    if (keep_a && keep_b) {
      if (a_first) {
        out.push_back({FeedSide::A, ev});
        out.push_back({FeedSide::B, ev});
      } else {
        out.push_back({FeedSide::B, ev});
        out.push_back({FeedSide::A, ev});
      }
    } else if (keep_a) {
      out.push_back({FeedSide::A, ev});
    } else {
      out.push_back({FeedSide::B, ev});
    }
  }

  if (counts != nullptr) {
    *counts = local;
  }
  return out;
}

}  // namespace mf::phase2
