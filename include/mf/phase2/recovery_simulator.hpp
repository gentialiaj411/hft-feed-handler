#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/phase2/recovery.hpp"

namespace mf::phase2 {

class ReplayRecoveryStore {
 public:
  void record_event(const mf::core::BookEvent& ev);
  [[nodiscard]] std::optional<mf::core::BookEvent> lookup(mf::core::Venue venue, std::uint64_t sequence) const;

 private:
  std::array<std::map<std::uint64_t, mf::core::BookEvent>, 3> by_venue_{};
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
