#pragma once

#include <cstdint>
#include <deque>

#include "mf/core/types.hpp"

namespace mf::research {

class OfiSignal {
 public:
  struct Config {
    std::uint64_t max_events{128};
    std::uint64_t max_window_ns{0};
  };

  OfiSignal();
  explicit OfiSignal(Config cfg);

  void update(const mf::core::BookEvent& ev);
  double value() const noexcept { return sum_; }

 private:
  struct Entry {
    std::uint64_t ts_ns{0};
    double v{0.0};
  };

  void prune(std::uint64_t ts_ns);

  Config cfg_{};
  std::deque<Entry> window_{};
  double sum_{0.0};

  bool has_bid_{false};
  bool has_ask_{false};
  std::uint32_t bid_px_{0};
  std::uint32_t bid_qty_{0};
  std::uint32_t ask_px_{0};
  std::uint32_t ask_qty_{0};
};

}  // namespace mf::research
