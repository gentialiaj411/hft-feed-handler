#include <cassert>
#include <cstdio>

#include "mf/os/numa.hpp"

#if defined(__linux__) && defined(MF_HAS_LIBNUMA)
#include <numaif.h>
#endif

int main() {
#if !defined(__linux__)
  std::printf("SKIP: Linux-only\n");
  return 0;
#elif !defined(MF_HAS_LIBNUMA)
  std::printf("SKIP: libnuma not present\n");
  return 0;
#else
  if (!mf::os::numa_available()) {
    std::printf("SKIP: numa unavailable\n");
    return 0;
  }
  constexpr std::size_t kBytes = 4096;
  void* p = mf::os::numa_alloc_on_node(kBytes, 0);
  assert(p != nullptr);
  int mode = 0;
  unsigned long nodemask = 0;
  constexpr unsigned long maxnode = 8 * sizeof(unsigned long);
  long rc = ::get_mempolicy(&mode, &nodemask, maxnode, p, MPOL_F_ADDR | MPOL_F_NODE);
  assert(rc == 0);
  mf::os::numa_free(p, kBytes);
  return 0;
#endif
}
