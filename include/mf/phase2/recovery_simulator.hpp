#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/phase2/recovery.hpp"

namespace mf::phase2 {

class ReplayRecoveryStore {
 public:
  explicit ReplayRecoveryStore(std::size_t max_events_per_venue = 512) : max_events_per_venue_(max_events_per_venue) {}
  void record_event(const mf::core::BookEvent& ev);
  void evict_before(mf::core::Venue venue, std::uint64_t min_sequence_to_keep);
  [[nodiscard]] std::optional<mf::core::BookEvent> lookup(mf::core::Venue venue, std::uint64_t sequence) const;

 private:
  std::size_t max_events_per_venue_{512};
  std::array<std::deque<mf::core::BookEvent>, 3> by_venue_{};
};

class ReplayRecoverySimulator final : public IRecoveryHandler {
 public:
  explicit ReplayRecoverySimulator(const ReplayRecoveryStore* store) : store_(store) {}

  void request_recovery(const RecoveryRequest& req) noexcept override;

  [[nodiscard]] std::vector<mf::core::BookEvent> drain_recovered() noexcept;
  [[nodiscard]] std::uint64_t requests_total() const noexcept { return requests_total_; }
  [[nodiscard]] std::uint64_t recovered_events_total() const noexcept { return recovered_events_total_; }

 private:
  const ReplayRecoveryStore* store_{nullptr};
  std::vector<mf::core::BookEvent> pending_recovered_{};
  std::uint64_t requests_total_{0};
  std::uint64_t recovered_events_total_{0};
};

}  // namespace mf::phase2
