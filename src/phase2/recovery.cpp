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

}  // namespace mf::phase2
