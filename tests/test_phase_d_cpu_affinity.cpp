#include <cassert>
#include <cstdio>

#include "mf/os/cpu_affinity.hpp"

#if defined(__linux__)
#include <sched.h>
#include <unistd.h>
#endif

int main() {
#if !defined(__linux__)
  std::printf("SKIP: Linux-only\n");
  return 0;
#else
  if (::sysconf(_SC_NPROCESSORS_ONLN) <= 1) {
    std::printf("SKIP: single CPU\n");
    return 0;
  }
  if (!mf::os::pin_current_thread(0)) {
    std::printf("SKIP: pin failed errno=%d\n", mf::os::last_errno());
    return 0;
  }
  assert(::sched_getcpu() == 0);
  return 0;
#endif
}
