#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "mf/live/bitfinex/lowering.hpp"
#include "mf/live/bitfinex/snapshot_sidecar.hpp"
#include "mf/live/bitfinex/wire_types.hpp"

namespace mf::live::bitfinex {

enum class FeedState { Gapped, Live };

enum class FeedAction { None, RequestResubscribe };

struct FeedStats {
  std::uint64_t gaps{0};
  std::uint64_t resyncs{0};
  std::uint64_t reconnects{0};
  std::uint64_t snapshots{0};
  std::uint64_t updates_applied{0};
};

class ResyncStateMachine {
 public:
  using EmitFn = std::function<void(const mf::core::BookEvent&)>;

  ResyncStateMachine(std::string symbol_wire, EmitFn emit);

  [[nodiscard]] FeedState state() const noexcept { return state_; }
  [[nodiscard]] const FeedStats& stats() const noexcept { return stats_; }
  [[nodiscard]] const OnBookTracker& on_book() const noexcept { return on_book_; }

  void on_disconnect() noexcept;
  FeedAction on_snapshot(const SnapshotFrame& frame, std::uint64_t ts_ns, const std::string& sidecar_path) noexcept;
  FeedAction on_update(const UpdateFrame& frame, std::uint64_t ts_ns) noexcept;

 private:
  std::string symbol_wire_{};
  EmitFn emit_{};
  FeedState state_{FeedState::Gapped};
  FeedStats stats_{};
  OnBookTracker on_book_{};
  LoweringStats lowering_stats_{};
  std::uint64_t last_seq_{0};
  bool have_seq_{false};
};

}  // namespace mf::live::bitfinex
