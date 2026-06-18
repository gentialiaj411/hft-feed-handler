#include "mf/phase2/pipeline.hpp"

#include "mf/core/book_event_crc.hpp"
#include <cassert>

namespace mf::phase2 {

Pipeline::Pipeline(std::uint64_t gap_window, std::size_t per_venue_capacity)
    : recovery_store_(static_cast<std::size_t>(gap_window + kRecoveryLookbackSlack)),
      recovery_sim_(&recovery_store_),
      sequencer_(gap_window),
      merger_(per_venue_capacity) {
  sequencer_.set_recovery_handler(&recovery_sim_);
}

void Pipeline::on_event(const mf::core::BookEvent& ev) {
  recovery_store_.record_event(ev);
  process_event(ev);
  auto recovered = recovery_sim_.drain_recovered();
  for (const auto& r : recovered) {
    const auto& pending = pending_by_venue_[venue_index(r.venue)];
    if (pending.find(r.sequence) != pending.end()) {
      continue;
    }
    process_event(r);
    ++stats_.recovery_reinjected;
  }
}

void Pipeline::finalize(IMergedEventSink* sink) {
  mf::core::BookEvent ev{};
  while (merger_.pop_next(ev)) {
    if (sink != nullptr) {
      sink->on_merged_event(ev);
    }
    mf::core::update_crc_from_book_event(stats_.merged_crc, ev);
  }
  stats_.recovery_requests = recovery_sim_.requests_total();
}

std::size_t Pipeline::venue_index(mf::core::Venue venue) noexcept {
  const std::size_t idx = static_cast<std::size_t>(static_cast<std::uint8_t>(venue));
  assert(idx < mf::core::kVenueSlotCount);
  return idx;
}

void Pipeline::evict_pending_before(mf::core::Venue venue, std::uint64_t seq) {
  auto& pending = pending_by_venue_[venue_index(venue)];
  pending.erase(pending.begin(), pending.lower_bound(seq));
}

void Pipeline::process_event(const mf::core::BookEvent& ev) {
  auto result = sequencer_.on_sequence(ev.venue, ev.sequence);

  if (result.update.status == SequenceStatus::DuplicateOrOld) {
    ++stats_.dropped_duplicate_or_old;
    return;
  }
  if (result.update.status == SequenceStatus::GapTooLarge) {
    ++stats_.dropped_gap_too_large;
    sequencer_.force_advance(ev.venue, ev.sequence);
    auto& pending = pending_by_venue_[venue_index(ev.venue)];
    const auto before = pending.size();
    evict_pending_before(ev.venue, ev.sequence);
    stats_.dropped_gap_too_large_pending_evicted += (before - pending.size());
    const std::uint64_t next = sequencer_.next_expected(ev.venue);
    if (next > kRecoveryLookbackSlack) {
      recovery_store_.evict_before(ev.venue, next - kRecoveryLookbackSlack);
    }
    return;
  }

  auto& pending = pending_by_venue_[venue_index(ev.venue)];
  if (result.update.status == SequenceStatus::GapBuffered) {
    pending[ev.sequence] = ev;
    ++stats_.buffered_out_of_order;
    return;
  }

  if (result.update.status == SequenceStatus::InOrder) {
    publish(ev);
    std::uint64_t to_release = (result.update.released_count > 0) ? (result.update.released_count - 1) : 0;
    std::uint64_t seq = ev.sequence + 1;
    while (to_release > 0) {
      auto it = pending.find(seq);
      if (it == pending.end()) {
        ++stats_.pending_inconsistency;
        ++seq;
        --to_release;
        continue;
      }
      publish(it->second);
      pending.erase(it);
      ++seq;
      --to_release;
    }
    const std::uint64_t next = sequencer_.next_expected(ev.venue);
    if (next > kRecoveryLookbackSlack) {
      recovery_store_.evict_before(ev.venue, next - kRecoveryLookbackSlack);
    }
  }
}

void Pipeline::publish(const mf::core::BookEvent& ev) {
  if (merger_.push(ev)) {
    ++stats_.accepted;
    return;
  }
  ++stats_.dropped_publish_overflow;
}

}  // namespace mf::phase2
