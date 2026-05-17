#include "mf/phase3/feature_bridge.hpp"

namespace mf::phase3 {

void FeatureBridge::on_merged_event(const mf::core::BookEvent& ev) noexcept {
  const auto apply = books_.on_event(ev);
  const std::uint64_t symbol = ev.symbol.as_u64();
  const Nbbo nbbo = nbbo_.update_and_current(symbol, ev.venue, apply.top_after);

  auto fv = features_.on_event(ev, nbbo, apply.queue_ahead_before_add);
  if (!fv.has_value()) {
    return;
  }
  ++stats_.feature_updates;

  if (publisher_ == nullptr) {
    ++stats_.dropped;
    return;
  }
  if (publisher_->try_publish(*fv)) {
    ++stats_.published;
  } else {
    ++stats_.dropped;
  }
}

}  // namespace mf::phase3
