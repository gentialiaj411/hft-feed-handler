#include <cassert>
#include <cstdint>
#include <cstdio>

#include "mf/core/spsc_ring_aligned_storage.hpp"

// NOTE: do not wrap side-effecting calls in assert(). With NDEBUG (Release /
// RelWithDebInfo) the assert macro elides its argument entirely, so the
// try_pop call would never happen and the ring would fill after 1024 pushes
// while the next try_push spins forever.

int main() {
  mf::core::SpscBackingInfo info{};
  auto ring = mf::core::make_spsc_ring_on_node<std::uint64_t, 1024>(0, false, info);
  if (ring == nullptr) {
    std::fprintf(stderr, "make_spsc_ring_on_node returned nullptr\n");
    return 1;
  }
  for (std::uint64_t i = 0; i < 10000; ++i) {
    while (!ring->try_push(i)) {}
    std::uint64_t out = 0;
    const bool popped = ring->try_pop(out);
    if (!popped) {
      std::fprintf(stderr, "try_pop returned false at i=%llu\n",
                   static_cast<unsigned long long>(i));
      return 1;
    }
    if (out != i) {
      std::fprintf(stderr, "spsc round-trip mismatch at i=%llu (got=%llu)\n",
                   static_cast<unsigned long long>(i),
                   static_cast<unsigned long long>(out));
      return 1;
    }
  }
  return 0;
}
