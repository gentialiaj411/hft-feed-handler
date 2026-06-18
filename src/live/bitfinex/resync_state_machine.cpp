#include "mf/live/bitfinex/resync_state_machine.hpp"

namespace mf::live::bitfinex {

ResyncStateMachine::ResyncStateMachine(std::string symbol_wire, EmitFn emit)
    : symbol_wire_(std::move(symbol_wire)), emit_(std::move(emit)) {}

void ResyncStateMachine::on_disconnect() noexcept {
  state_ = FeedState::Gapped;
  have_seq_ = false;
  last_seq_ = 0;
  ++stats_.reconnects;
}

FeedAction ResyncStateMachine::on_snapshot(
    const SnapshotFrame& frame,
    std::uint64_t ts_ns,
    const std::string& sidecar_path) noexcept {
  (void)frame.channel_id;
  on_book_.clear();
  if (frame.sequence != 0) {
    last_seq_ = frame.sequence;
    have_seq_ = true;
  } else {
    last_seq_ = 0;
    have_seq_ = false;
  }

  SnapshotSidecar sidecar{};
  sidecar.symbol = symbol_wire_;
  sidecar.cut_sequence = frame.sequence;
  sidecar.exchange_ts_ns = ts_ns;
  sidecar.rows = frame.rows;
  (void)write_sidecar(sidecar_path, sidecar);

  const auto bootstrap = lower_snapshot_rows(frame.rows, ts_ns, symbol_wire_, on_book_, lowering_stats_);
  for (const auto& ev : bootstrap) {
    if (emit_) emit_(ev);
  }

  state_ = FeedState::Live;
  ++stats_.snapshots;
  ++stats_.resyncs;
  return FeedAction::None;
}

FeedAction ResyncStateMachine::on_update(const UpdateFrame& frame, std::uint64_t ts_ns) noexcept {
  if (state_ != FeedState::Live) {
    return FeedAction::RequestResubscribe;
  }

  if (frame.sequence != 0) {
    if (!have_seq_ || frame.sequence != last_seq_ + 1) {
      ++stats_.gaps;
      state_ = FeedState::Gapped;
      have_seq_ = false;
      last_seq_ = 0;
      return FeedAction::RequestResubscribe;
    }
    last_seq_ = frame.sequence;
  }

  const auto ev = lower_row(frame.row, frame.sequence, ts_ns, symbol_wire_, on_book_, lowering_stats_);
  if (ev.has_value() && emit_) {
    emit_(ev.value());
    ++stats_.updates_applied;
  }
  return FeedAction::None;
}

}  // namespace mf::live::bitfinex
