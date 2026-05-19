#include "mf/phase3/book_snapshot.hpp"

#include "mf/phase3/order_book_engine.hpp"

namespace mf::phase3 {

BookReconciler::BookReconciler(std::uint64_t snapshot_interval)
    : snapshot_interval_(snapshot_interval == 0 ? 1 : snapshot_interval),
      live_(std::make_unique<OrderBookEngine>()) {
  stats_.snapshot_interval = snapshot_interval_;
}

BookReconciler::~BookReconciler() = default;

const OrderBookEngine& BookReconciler::live_engine() const noexcept {
  return *live_;
}

BookReconcileStats BookReconciler::on_event(const mf::core::BookEvent& ev) {
  (void)live_->on_event(ev);
  consumed_.push_back(ev);
  touched_.insert(VenueSymbol{ev.venue, ev.symbol.as_u64()});
  ++stats_.events_processed;

  if ((stats_.events_processed % snapshot_interval_) == 0U) {
    check_snapshot();
  }
  return stats_;
}

void BookReconciler::check_snapshot() {
  OrderBookEngine rebuilt{};
  for (const auto& ev : consumed_) {
    (void)rebuilt.on_event(ev);
  }

  for (const auto& key : touched_) {
    const auto live_snapshot = live_->snapshot(key.venue, key.symbol);
    const auto rebuilt_snapshot = rebuilt.snapshot(key.venue, key.symbol);
    ++stats_.snapshots_checked;
    if (!byte_equal(live_snapshot, rebuilt_snapshot)) {
      ++stats_.divergences;
    }
  }
}

}  // namespace mf::phase3
