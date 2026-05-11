#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/phase2/deterministic_merger.hpp"
#include "mf/phase2/recovery.hpp"

namespace {

class CaptureRecoveryHandler final : public mf::phase2::IRecoveryHandler {
 public:
  void request_recovery(const mf::phase2::RecoveryRequest& req) noexcept override {
    requests.push_back(req);
  }

  std::vector<mf::phase2::RecoveryRequest> requests{};
};

mf::core::BookEvent make_event(
    mf::core::Venue venue,
    std::uint64_t seq,
    std::uint64_t exchange_ts_ns,
    std::uint8_t raw_type) {
  mf::core::BookEvent ev{};
  ev.venue = venue;
  ev.sequence = seq;
  ev.exchange_ts_ns = exchange_ts_ns;
  ev.raw_type = raw_type;
  return ev;
}

void test_recovery_requests() {
  mf::phase2::GapAwareSequencer sequencer(2);
  CaptureRecoveryHandler capture;
  sequencer.set_recovery_handler(&capture);

  (void)sequencer.on_sequence(mf::core::Venue::Nasdaq, 1);
  const auto gap_small = sequencer.on_sequence(mf::core::Venue::Nasdaq, 3);
  assert(gap_small.recovery.has_value());
  assert(gap_small.recovery->reason == mf::phase2::RecoveryReason::GapDetected);
  assert(gap_small.recovery->missing.begin == 2);
  assert(gap_small.recovery->missing.end == 2);

  const auto gap_large = sequencer.on_sequence(mf::core::Venue::Nasdaq, 7);
  assert(gap_large.recovery.has_value());
  assert(gap_large.recovery->reason == mf::phase2::RecoveryReason::GapTooLarge);
  assert(gap_large.recovery->missing.begin == 2);
  assert(gap_large.recovery->missing.end == 6);

  assert(capture.requests.size() == 2);
}

void test_deterministic_merge_order() {
  mf::phase2::DeterministicMerger merger(16);
  assert(merger.push(make_event(mf::core::Venue::Iex, 1, 1000, static_cast<std::uint8_t>('a'))));
  assert(merger.push(make_event(mf::core::Venue::Nasdaq, 10, 900, static_cast<std::uint8_t>('A'))));
  assert(merger.push(make_event(mf::core::Venue::Cboe, 3, 1000, static_cast<std::uint8_t>('P'))));
  assert(merger.push(make_event(mf::core::Venue::Nasdaq, 11, 1000, static_cast<std::uint8_t>('E'))));

  std::array<mf::core::BookEvent, 4> out{};
  for (std::size_t i = 0; i < out.size(); ++i) {
    assert(merger.pop_next(out[i]));
  }

  assert(out[0].exchange_ts_ns == 900);
  assert(out[0].venue == mf::core::Venue::Nasdaq);

  // Tie on exchange_ts_ns=1000 breaks by venue enum value, then sequence.
  assert(out[1].venue == mf::core::Venue::Nasdaq);
  assert(out[2].venue == mf::core::Venue::Iex);
  assert(out[3].venue == mf::core::Venue::Cboe);

  mf::core::BookEvent extra{};
  assert(!merger.pop_next(extra));
}

void test_per_venue_capacity() {
  mf::phase2::DeterministicMerger merger(1);
  assert(merger.push(make_event(mf::core::Venue::Nasdaq, 1, 100, 1)));
  assert(!merger.push(make_event(mf::core::Venue::Nasdaq, 2, 101, 2)));
  assert(merger.push(make_event(mf::core::Venue::Iex, 1, 100, 3)));
  assert(merger.queued_count() == 2);
}

}  // namespace

int main() {
  test_recovery_requests();
  test_deterministic_merge_order();
  test_per_venue_capacity();
  return 0;
}
