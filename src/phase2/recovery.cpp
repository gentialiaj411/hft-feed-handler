#include "mf/phase2/recovery.hpp"

namespace mf::phase2 {

SequencerResult GapAwareSequencer::on_sequence(mf::core::Venue venue, std::uint64_t seq) noexcept {
  SequencerResult out{};
  out.update = trackers_.on_sequence(venue, seq);

  if (!out.update.missing_range.has_value()) {
    return out;
  }

  RecoveryRequest req{};
  req.venue = venue;
  req.missing = *out.update.missing_range;
  req.reason = (out.update.status == SequenceStatus::GapTooLarge)
                   ? RecoveryReason::GapTooLarge
                   : RecoveryReason::GapDetected;
  out.recovery = req;

  if (recovery_handler_ != nullptr) {
    recovery_handler_->request_recovery(req);
  }
  return out;
}

void GapAwareSequencer::force_advance(mf::core::Venue venue, std::uint64_t new_next) noexcept {
  trackers_.force_advance(venue, new_next);
}

std::uint64_t GapAwareSequencer::next_expected(mf::core::Venue venue) const noexcept {
  return trackers_.tracker_for(venue).next_expected();
}

}  // namespace mf::phase2
