#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "mf/core/types.hpp"

namespace mf::phase2 {

enum class SequenceStatus : std::uint8_t {
  InOrder = 0,
  DuplicateOrOld = 1,
  GapBuffered = 2,
  GapTooLarge = 3,
};

struct GapRange {
  std::uint64_t begin{0};
  std::uint64_t end{0};
};

struct SequenceUpdate {
  SequenceStatus status{SequenceStatus::InOrder};
  std::uint64_t input_sequence{0};
  std::uint64_t expected_before{0};
  std::uint64_t expected_after{0};
  std::size_t released_count{0};
  std::optional<GapRange> missing_range{};
};

class SequenceTracker {
 public:
  explicit SequenceTracker(std::uint64_t gap_window);

  [[nodiscard]] SequenceUpdate on_sequence(std::uint64_t seq) noexcept;
  void force_advance(std::uint64_t new_next) noexcept;
  [[nodiscard]] std::uint64_t next_expected() const noexcept { return next_expected_; }
  [[nodiscard]] std::uint64_t gap_window() const noexcept { return gap_window_; }

 private:
  void mark_buffered(std::uint64_t seq) noexcept;
  bool pop_buffered(std::uint64_t seq) noexcept;

  std::uint64_t next_expected_{1};
  std::uint64_t gap_window_{0};
  std::vector<std::uint64_t> buffered_tags_{};
};

class MultiVenueSequenceTracker {
 public:
  explicit MultiVenueSequenceTracker(std::uint64_t gap_window);

  [[nodiscard]] SequenceUpdate on_sequence(mf::core::Venue venue, std::uint64_t seq) noexcept;
  void force_advance(mf::core::Venue venue, std::uint64_t new_next) noexcept;
  [[nodiscard]] const SequenceTracker& tracker_for(mf::core::Venue venue) const noexcept;

 private:
  std::size_t index_for(mf::core::Venue venue) const noexcept;
  std::vector<SequenceTracker> trackers_{};
};

}  // namespace mf::phase2
