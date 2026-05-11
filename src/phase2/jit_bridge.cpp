#include "mf/phase2/jit_bridge.hpp"

namespace mf::phase2 {

JitSignalEvent JitBridge::convert(const mf::core::BookEvent& ev) noexcept {
  JitSignalEvent out{};
  out.venue = static_cast<std::uint8_t>(ev.venue);
  out.type = static_cast<std::uint8_t>(ev.type);
  out.side = static_cast<std::uint8_t>(ev.side);
  out.raw_type = ev.raw_type;
  out.sequence = ev.sequence;
  out.exchange_ts_ns = ev.exchange_ts_ns;
  out.ingest_ts_ns = ev.ingest_ts_ns;
  out.symbol_u64 = ev.symbol.as_u64();
  out.order_id = ev.order_id;
  out.match_id = ev.match_id;
  out.qty = ev.qty;
  out.price = ev.price;
  return out;
}

void JitBridge::on_merged_event(const mf::core::BookEvent& ev) noexcept {
  if (publisher_ == nullptr) {
    ++stats_.dropped;
    return;
  }
  if (publisher_->try_publish(convert(ev))) {
    ++stats_.published;
  } else {
    ++stats_.dropped;
  }
}

}  // namespace mf::phase2
