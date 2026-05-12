#pragma once

#include <cstdint>
#include <optional>

#include "mf/core/types.hpp"
#include "mf/phase2/sequence_tracker.hpp"

namespace mf::phase2 {

enum class RecoveryReason : std::uint8_t {
  GapDetected = 0,
  GapTooLarge = 1,
};

struct RecoveryRequest {
  mf::core::Venue venue{mf::core::Venue::Nasdaq};
  GapRange missing{};
  RecoveryReason reason{RecoveryReason::GapDetected};
};

class IRecoveryHandler {
 public:
  virtual ~IRecoveryHandler() = default;
  virtual void request_recovery(const RecoveryRequest& req) noexcept = 0;
};

struct SequencerResult {
  SequenceUpdate update{};
  std::optional<RecoveryRequest> recovery{};
};

class GapAwareSequencer {
 public:
  explicit GapAwareSequencer(std::uint64_t gap_window)
      : trackers_(gap_window) {}

  [[nodiscard]] SequencerResult on_sequence(mf::core::Venue venue, std::uint64_t seq) noexcept;
  void set_recovery_handler(IRecoveryHandler* handler) noexcept { recovery_handler_ = handler; }
  void force_advance(mf::core::Venue venue, std::uint64_t new_next) noexcept;
  [[nodiscard]] std::uint64_t next_expected(mf::core::Venue venue) const noexcept;

 private:
  MultiVenueSequenceTracker trackers_;
  IRecoveryHandler* recovery_handler_{nullptr};
};

}  // namespace mf::phase2
