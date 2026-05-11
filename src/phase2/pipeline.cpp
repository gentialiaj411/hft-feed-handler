#include "mf/phase2/pipeline.hpp"

#include "mf/core/crc32.hpp"

namespace mf::phase2 {

namespace {
void update_crc_from_event(std::uint32_t& crc, const mf::core::BookEvent& ev) {
  crc = mf::core::crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.venue), sizeof(ev.venue));
  crc = mf::core::crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.type), sizeof(ev.type));
  crc = mf::core::crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.sequence), sizeof(ev.sequence));
  crc = mf::core::crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.exchange_ts_ns), sizeof(ev.exchange_ts_ns));
  crc = mf::core::crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.symbol), sizeof(ev.symbol));
  crc = mf::core::crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.order_id), sizeof(ev.order_id));
  crc = mf::core::crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.qty), sizeof(ev.qty));
  crc = mf::core::crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.price), sizeof(ev.price));
}
}  // namespace

Pipeline::Pipeline(std::uint64_t gap_window, std::size_t per_venue_capacity)
    : recovery_sim_(&recovery_store_), sequencer_(gap_window), merger_(per_venue_capacity) {
  sequencer_.set_recovery_handler(&recovery_sim_);
}

void Pipeline::on_event(const mf::core::BookEvent& ev) {
  recovery_store_.record_event(ev);
  process_event(ev);
  auto recovered = recovery_sim_.drain_recovered();
  for (const auto& r : recovered) {
    process_event(r);
    ++stats_.recovery_reinjected;
  }
}

void Pipeline::finalize() {
  mf::core::BookEvent ev{};
  while (merger_.pop_next(ev)) {
    update_crc_from_event(stats_.merged_crc, ev);
  }
  stats_.recovery_requests = recovery_sim_.requests_total();
}

std::size_t Pipeline::venue_index(mf::core::Venue venue) noexcept {
  return static_cast<std::size_t>(static_cast<std::uint8_t>(venue));
}

void Pipeline::process_event(const mf::core::BookEvent& ev) {
  auto result = sequencer_.on_sequence(ev.venue, ev.sequence);

  if (result.update.status == SequenceStatus::DuplicateOrOld) {
    ++stats_.dropped_duplicate_or_old;
    return;
  }
  if (result.update.status == SequenceStatus::GapTooLarge) {
    ++stats_.dropped_gap_too_large;
    return;
  }

  auto& pending = pending_by_venue_[venue_index(ev.venue)];
  if (result.update.status == SequenceStatus::GapBuffered) {
    pending[ev.sequence] = ev;
    ++stats_.buffered_out_of_order;
    return;
  }

  if (result.update.status == SequenceStatus::InOrder) {
    publish(ev);
    std::uint64_t to_release = (result.update.released_count > 0) ? (result.update.released_count - 1) : 0;
    std::uint64_t seq = ev.sequence + 1;
    while (to_release > 0) {
      auto it = pending.find(seq);
      if (it == pending.end()) {
        break;
      }
      publish(it->second);
      pending.erase(it);
      ++seq;
      --to_release;
    }
  }
}

void Pipeline::publish(const mf::core::BookEvent& ev) {
  if (merger_.push(ev)) {
    ++stats_.accepted;
    return;
  }
  ++stats_.dropped_publish_overflow;
}

}  // namespace mf::phase2
