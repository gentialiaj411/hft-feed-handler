#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/phase2/recovery.hpp"

namespace mf::phase2 {

enum class FeedSide : std::uint8_t {
  A = 0,
  B = 1,
};

struct AbArbiterStats {
  std::uint64_t accepted{0};
  std::uint64_t accepted_a{0};
  std::uint64_t accepted_b{0};
  std::uint64_t duplicate_or_old{0};
  std::uint64_t gap_buffered{0};
  std::uint64_t gap_too_large{0};
  std::uint64_t forced_advances{0};
};

struct AbArbiterResult {
  bool accepted{false};
  SequenceStatus status{SequenceStatus::DuplicateOrOld};
  std::optional<RecoveryRequest> recovery{};
};

class AbArbiter {
 public:
  explicit AbArbiter(std::uint64_t gap_window) : sequencer_(gap_window) {}

  [[nodiscard]] AbArbiterResult on_event(FeedSide side, const mf::core::BookEvent& ev) noexcept;
  [[nodiscard]] std::vector<mf::core::BookEvent> drain_ready() noexcept;
  [[nodiscard]] const AbArbiterStats& stats() const noexcept { return stats_; }

 private:
  static std::size_t venue_index(mf::core::Venue venue) noexcept;

  GapAwareSequencer sequencer_;
  std::array<std::vector<mf::core::BookEvent>, mf::core::kVenueSlotCount> pending_{};
  std::vector<mf::core::BookEvent> ready_{};
  AbArbiterStats stats_{};
};

struct DualFeedDropConfig {
  double drop_rate_a{0.0};
  double drop_rate_b{0.0};
  std::uint64_t seed{1};
};

struct DroppedFeedCounts {
  std::uint64_t dropped_a{0};
  std::uint64_t dropped_b{0};
};

std::vector<std::pair<FeedSide, mf::core::BookEvent>> make_dual_feed_race_stream(
    const std::vector<mf::core::BookEvent>& source,
    const DualFeedDropConfig& cfg,
    DroppedFeedCounts* counts = nullptr);

}  // namespace mf::phase2
