#include "mf/phase2/deterministic_merger.hpp"

#include <cassert>

namespace mf::phase2 {

namespace {
constexpr std::size_t kTrackedVenueCount = mf::core::kVenueSlotCount;
static_assert(static_cast<std::uint8_t>(mf::core::Venue::Cboe) == 2, "Venue enum/layout drifted.");
}

std::size_t DeterministicMerger::index_for(mf::core::Venue venue) noexcept {
  const std::size_t idx = static_cast<std::size_t>(static_cast<std::uint8_t>(venue));
  assert(idx < kTrackedVenueCount);
  return idx;
}

bool DeterministicMerger::less_event(const mf::core::BookEvent& a, const mf::core::BookEvent& b) noexcept {
  if (a.exchange_ts_ns != b.exchange_ts_ns) {
    return a.exchange_ts_ns < b.exchange_ts_ns;
  }
  if (a.venue != b.venue) {
    return static_cast<std::uint8_t>(a.venue) < static_cast<std::uint8_t>(b.venue);
  }
  if (a.sequence != b.sequence) {
    return a.sequence < b.sequence;
  }
  return a.raw_type < b.raw_type;
}

bool DeterministicMerger::push(const mf::core::BookEvent& ev) noexcept {
  // per_venue_capacity_ is currently a sizing hint rather than a hard cap:
  // callers rely on pushes never being rejected (Phase 2 Pipeline funnels
  // events through here as part of its in-order publication loop and counts
  // any false return as a publication overflow). If we ever wire up a real
  // backpressure path the check should land here and the call sites that
  // expect unconditional acceptance must move to the unbounded constructor.
  queues_[index_for(ev.venue)].push_back(ev);
  return true;
}

bool DeterministicMerger::pop_next(mf::core::BookEvent& out) noexcept {
  std::size_t best_index = queues_.size();
  for (std::size_t i = 0; i < queues_.size(); ++i) {
    if (queues_[i].empty()) {
      continue;
    }
    if (best_index == queues_.size() || less_event(queues_[i].front(), queues_[best_index].front())) {
      best_index = i;
    }
  }

  if (best_index == queues_.size()) {
    return false;
  }

  out = queues_[best_index].front();
  queues_[best_index].pop_front();
  return true;
}

std::size_t DeterministicMerger::queued_count() const noexcept {
  std::size_t n = 0;
  for (const auto& q : queues_) {
    n += q.size();
  }
  return n;
}

}  // namespace mf::phase2
