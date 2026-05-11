#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>

#include "mf/core/types.hpp"

namespace mf::phase2 {

class DeterministicMerger {
 public:
  explicit DeterministicMerger(std::size_t per_venue_capacity)
      : per_venue_capacity_(per_venue_capacity) {}

  [[nodiscard]] bool push(const mf::core::BookEvent& ev) noexcept;
  [[nodiscard]] bool pop_next(mf::core::BookEvent& out) noexcept;
  [[nodiscard]] std::size_t queued_count() const noexcept;

 private:
  static std::size_t index_for(mf::core::Venue venue) noexcept;
  static bool less_event(const mf::core::BookEvent& a, const mf::core::BookEvent& b) noexcept;

  std::size_t per_venue_capacity_{0};
  std::array<std::deque<mf::core::BookEvent>, 3> queues_{};
};

}  // namespace mf::phase2
