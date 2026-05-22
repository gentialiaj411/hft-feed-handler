#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <intrin.h>
#include <windows.h>
#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
#include <x86intrin.h>
#endif

namespace mf::bench {

inline std::uint64_t tsc_now() noexcept {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
  unsigned int aux = 0;
  return __rdtscp(&aux);
#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
  unsigned int aux = 0;
  return __rdtscp(&aux);
#else
  return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

inline double calibrate_ticks_per_ns(std::chrono::milliseconds duration = std::chrono::milliseconds(100)) {
#if defined(_WIN32)
  LARGE_INTEGER freq{};
  LARGE_INTEGER q0{};
  LARGE_INTEGER q1{};
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&q0);
  const std::uint64_t t0 = tsc_now();
  std::this_thread::sleep_for(duration);
  const std::uint64_t t1 = tsc_now();
  QueryPerformanceCounter(&q1);
  const double elapsed_ns = static_cast<double>(q1.QuadPart - q0.QuadPart) *
                            1'000'000'000.0 / static_cast<double>(freq.QuadPart);
  return static_cast<double>(t1 - t0) / elapsed_ns;
#elif defined(__x86_64__) || defined(__i386__)
  const auto c0 = std::chrono::steady_clock::now();
  const std::uint64_t t0 = tsc_now();
  std::this_thread::sleep_for(duration);
  const std::uint64_t t1 = tsc_now();
  const auto c1 = std::chrono::steady_clock::now();
  const double elapsed_ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(c1 - c0).count());
  return static_cast<double>(t1 - t0) / elapsed_ns;
#else
  (void)duration;
  return 1.0;
#endif
}

inline std::uint64_t ticks_to_ns(std::uint64_t ticks, double ticks_per_ns) noexcept {
  if (ticks_per_ns <= 0.0) {
    return ticks;
  }
  return static_cast<std::uint64_t>(static_cast<double>(ticks) / ticks_per_ns);
}

}  // namespace mf::bench
