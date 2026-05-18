#include "mf/os/cpu_affinity.hpp"

#include <atomic>
#include <cerrno>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace mf::os {

namespace {
std::atomic<int> g_last_errno{0};
}

int last_errno() noexcept { return g_last_errno.load(std::memory_order_relaxed); }

bool pin_current_thread(int cpu) noexcept {
#if defined(__linux__)
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpu, &cpuset);
  const int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
  g_last_errno.store((rc == 0) ? 0 : rc, std::memory_order_relaxed);
  return rc == 0;
#else
  (void)cpu;
  g_last_errno.store(ENOTSUP, std::memory_order_relaxed);
  return false;
#endif
}

bool set_thread_name(const std::string& name) noexcept {
#if defined(__linux__)
  const int rc = pthread_setname_np(pthread_self(), name.c_str());
  g_last_errno.store((rc == 0) ? 0 : rc, std::memory_order_relaxed);
  return rc == 0;
#else
  (void)name;
  g_last_errno.store(ENOTSUP, std::memory_order_relaxed);
  return false;
#endif
}

bool set_realtime_fifo(int priority) noexcept {
#if defined(__linux__)
  sched_param sp{};
  sp.sched_priority = priority;
  const int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
  g_last_errno.store((rc == 0) ? 0 : rc, std::memory_order_relaxed);
  return rc == 0;
#else
  (void)priority;
  g_last_errno.store(ENOTSUP, std::memory_order_relaxed);
  return false;
#endif
}

}  // namespace mf::os
