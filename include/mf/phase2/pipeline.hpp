#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>

#include "mf/core/types.hpp"
#include "mf/phase2/deterministic_merger.hpp"
#include "mf/phase2/recovery.hpp"
#include "mf/phase2/recovery_simulator.hpp"

namespace mf::phase2 {

struct PipelineStats {
  std::uint64_t accepted{0};
  std::uint64_t dropped_publish_overflow{0};
  std::uint64_t dropped_duplicate_or_old{0};
  std::uint64_t buffered_out_of_order{0};
  std::uint64_t dropped_gap_too_large{0};
  std::uint64_t recovery_requests{0};
  std::uint64_t recovery_reinjected{0};
  std::uint32_t merged_crc{0};
};

class Pipeline {
 public:
  Pipeline(std::uint64_t gap_window, std::size_t per_venue_capacity);

  void on_event(const mf::core::BookEvent& ev);
  void finalize();
  [[nodiscard]] const PipelineStats& stats() const noexcept { return stats_; }

 private:
  static std::size_t venue_index(mf::core::Venue venue) noexcept;
  void process_event(const mf::core::BookEvent& ev);
  void publish(const mf::core::BookEvent& ev);

  ReplayRecoveryStore recovery_store_{};
  ReplayRecoverySimulator recovery_sim_;
  GapAwareSequencer sequencer_;
  DeterministicMerger merger_;
  std::array<std::map<std::uint64_t, mf::core::BookEvent>, 3> pending_by_venue_{};
  PipelineStats stats_{};
};

}  // namespace mf::phase2
