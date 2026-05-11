#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

#include "mf/core/crc32.hpp"
#include "mf/core/types.hpp"
#include "mf/phase2/deterministic_merger.hpp"

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

mf::core::BookEvent make_event(
    mf::core::Venue venue,
    std::uint64_t sequence,
    std::uint64_t exchange_ts_ns,
    std::uint8_t raw_type,
    std::uint64_t order_id) {
  mf::core::BookEvent ev{};
  ev.venue = venue;
  ev.type = mf::core::EventType::Add;
  ev.sequence = sequence;
  ev.exchange_ts_ns = exchange_ts_ns;
  ev.raw_type = raw_type;
  ev.order_id = order_id;
  ev.qty = 100;
  ev.price = 10000;
  return ev;
}

std::uint32_t merged_crc_for_order(const std::vector<mf::core::BookEvent>& in) {
  mf::phase2::DeterministicMerger merger(64);
  for (const auto& ev : in) {
    if (!merger.push(ev)) {
      return 0;
    }
  }
  std::uint32_t crc = 0;
  mf::core::BookEvent out{};
  std::size_t guard = 0;
  while (merger.pop_next(out)) {
    if (++guard > in.size() + 8) {
      return 0;
    }
    update_crc_from_event(crc, out);
  }
  return crc;
}

bool test_merge_crc_invariant_under_arrival_permutations() {
  const mf::core::BookEvent n10 = make_event(mf::core::Venue::Nasdaq, 10, 100000, static_cast<std::uint8_t>('A'), 1);
  const mf::core::BookEvent n11 = make_event(mf::core::Venue::Nasdaq, 11, 100002, static_cast<std::uint8_t>('E'), 4);
  const mf::core::BookEvent i200 = make_event(mf::core::Venue::Iex, 200, 100000, static_cast<std::uint8_t>('a'), 2);
  const mf::core::BookEvent i201 = make_event(mf::core::Venue::Iex, 201, 100003, static_cast<std::uint8_t>('M'), 5);
  const mf::core::BookEvent c900 = make_event(mf::core::Venue::Cboe, 900, 100001, static_cast<std::uint8_t>('P'), 3);

  const std::vector<mf::core::BookEvent> base = {n10, i200, c900, n11, i201};

  const std::uint32_t reference_crc = merged_crc_for_order(base);

  // Permutations preserve within-venue sequence order:
  // NASDAQ: 10 then 11, IEX: 200 then 201, Cboe: 900.
  const std::vector<mf::core::BookEvent> perm1 = {i200, n10, c900, n11, i201};
  if (merged_crc_for_order(perm1) != reference_crc) return false;

  const std::vector<mf::core::BookEvent> perm2 = {c900, n10, i200, i201, n11};
  if (merged_crc_for_order(perm2) != reference_crc) return false;

  const std::vector<mf::core::BookEvent> perm3 = {n10, n11, i200, c900, i201};
  if (merged_crc_for_order(perm3) != reference_crc) return false;
  return true;
}

}  // namespace

int main() {
  if (!test_merge_crc_invariant_under_arrival_permutations()) {
    std::cerr << "phase2 determinism CRC permutation check failed\n";
    return 1;
  }
  return 0;
}
