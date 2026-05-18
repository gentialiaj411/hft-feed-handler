#include <cassert>
#include <cstdint>

#include "mf/core/spsc_ring_aligned_storage.hpp"

int main() {
  mf::core::SpscBackingInfo info{};
  auto ring = mf::core::make_spsc_ring_on_node<std::uint64_t, 1024>(0, false, info);
  assert(ring != nullptr);
  for (std::uint64_t i = 0; i < 10000; ++i) {
    while (!ring->try_push(i)) {}
    std::uint64_t out = 0;
    assert(ring->try_pop(out));
    assert(out == i);
  }
  return 0;
}
