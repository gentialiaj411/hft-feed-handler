#include "mf/phase2/recovery_simulator.hpp"

namespace mf::phase2 {

namespace {
std::size_t venue_index(mf::core::Venue venue) noexcept {
  return static_cast<std::size_t>(static_cast<std::uint8_t>(venue));
}
}  // namespace

void ReplayRecoveryStore::record_event(const mf::core::BookEvent& ev) {
  by_venue_[venue_index(ev.venue)][ev.sequence] = ev;
}

std::optional<mf::core::BookEvent> ReplayRecoveryStore::lookup(mf::core::Venue venue, std::uint64_t sequence) const {
  const auto& v = by_venue_[venue_index(venue)];
  auto it = v.find(sequence);
  if (it == v.end()) {
    return std::nullopt;
  }
  return it->second;
}

void ReplayRecoverySimulator::request_recovery(const RecoveryRequest& req) noexcept {
  ++requests_total_;
  if (store_ == nullptr) {
    return;
  }
  if (req.reason == RecoveryReason::GapTooLarge) {
    // In replay simulation mode we do not brute-force large missing spans.
    return;
  }
  for (std::uint64_t seq = req.missing.begin; seq <= req.missing.end; ++seq) {
    auto ev = store_->lookup(req.venue, seq);
    if (!ev.has_value()) {
      continue;
    }
    pending_recovered_.push_back(*ev);
    ++recovered_events_total_;
  }
}

std::vector<mf::core::BookEvent> ReplayRecoverySimulator::drain_recovered() noexcept {
  std::vector<mf::core::BookEvent> out;
  out.swap(pending_recovered_);
  return out;
}

}  // namespace mf::phase2
