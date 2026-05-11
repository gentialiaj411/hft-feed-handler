#include "mf/core/time.hpp"

#if defined(_WIN32)
#include <chrono>
#else
#include <ctime>
#endif

namespace mf::core {

std::uint64_t monotonic_raw_now_ns() noexcept {
#if defined(_WIN32)
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
#else
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<std::uint64_t>(ts.tv_nsec);
#endif
}

}  // namespace mf::core
