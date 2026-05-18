#include "mf/os/numa.hpp"

#include <cstdlib>

#if defined(__linux__) && defined(MF_HAS_LIBNUMA)
#include <numa.h>
#include <sched.h>
#endif

namespace mf::os {

namespace {
std::size_t align_up(std::size_t n, std::size_t align) noexcept {
  return ((n + align - 1U) / align) * align;
}
}

bool numa_available() noexcept {
#if defined(__linux__) && defined(MF_HAS_LIBNUMA)
  return ::numa_available() >= 0;
#else
  return false;
#endif
}

int current_numa_node() noexcept {
#if defined(__linux__) && defined(MF_HAS_LIBNUMA)
  if (!numa_available()) return -1;
  return ::numa_node_of_cpu(::sched_getcpu());
#else
  return -1;
#endif
}

int numa_node_of_cpu(int cpu) noexcept {
#if defined(__linux__) && defined(MF_HAS_LIBNUMA)
  if (!numa_available()) return -1;
  return ::numa_node_of_cpu(cpu);
#else
  (void)cpu;
  return -1;
#endif
}

void* numa_alloc_on_node(std::size_t bytes, int node) noexcept {
  if (bytes == 0) return nullptr;
#if defined(__linux__) && defined(MF_HAS_LIBNUMA)
  if (numa_available()) {
    return ::numa_alloc_onnode(bytes, node);
  }
#else
  (void)node;
#endif
  return std::aligned_alloc(64, align_up(bytes, 64));
}

void numa_free(void* ptr, std::size_t bytes) noexcept {
  if (ptr == nullptr) return;
#if defined(__linux__) && defined(MF_HAS_LIBNUMA)
  if (numa_available()) {
    ::numa_free(ptr, bytes);
    return;
  }
#else
  (void)bytes;
#endif
  std::free(ptr);
}

}  // namespace mf::os
