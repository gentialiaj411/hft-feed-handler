#include "mf/phase2/recovery_simulator.hpp"

#include <algorithm>
#include <cassert>

namespace mf::phase2 {

namespace {
std::size_t venue_index(mf::core::Venue venue) noexcept {
  const std::size_t idx = static_cast<std::size_t>(static_cast<std::uint8_t>(venue));
  assert(idx < mf::core::kVenueSlotCount);
  return idx;
}
}  // namespace

void ReplayRecoveryStore::record_event(const mf::core::BookEvent& ev) {
  auto& q = by_venue_[venue_index(ev.venue)];
  q.push_back(ev);
  while (q.size() > max_events_per_venue_) {
    q.pop_front();
  }
}

void ReplayRecoveryStore::evict_before(mf::core::Venue venue, std::uint64_t min_sequence_to_keep) {
  auto& q = by_venue_[venue_index(venue)];
  while (!q.empty() && q.front().sequence < min_sequence_to_keep) {
    q.pop_front();
  }
}

std::optional<mf::core::BookEvent> ReplayRecoveryStore::lookup(mf::core::Venue venue, std::uint64_t sequence) const {
  const auto& q = by_venue_[venue_index(venue)];
  const auto it = std::lower_bound(
      q.begin(),
      q.end(),
      sequence,
      [](const mf::core::BookEvent& ev, std::uint64_t seq) { return ev.sequence < seq; });
  if (it != q.end() && it->sequence == sequence) {
    return *it;
  }
  return std::nullopt;
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
