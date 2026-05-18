#pragma once

#include <cstdint>

#include "mf/core/types.hpp"

namespace mf::research {

class SimulationClock {
 public:
  void reset(std::uint64_t ts_ns = 0) noexcept {
    now_ns_ = ts_ns;
    events_seen_ = 0;
  }

  bool advance_to(std::uint64_t ts_ns) noexcept {
    if (ts_ns < now_ns_) {
      return false;
    }
    now_ns_ = ts_ns;
    ++events_seen_;
    return true;
  }

  bool on_event(const mf::core::BookEvent& ev) noexcept { return advance_to(ev.exchange_ts_ns); }

  [[nodiscard]] std::uint64_t now_ns() const noexcept { return now_ns_; }
  [[nodiscard]] std::uint64_t events_seen() const noexcept { return events_seen_; }

 private:
  std::uint64_t now_ns_{0};
  std::uint64_t events_seen_{0};
};

}  // namespace mf::research
