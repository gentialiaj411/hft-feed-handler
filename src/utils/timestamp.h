#pragma once

#include <chrono>
#include <cstdint>

namespace market {

inline uint64_t now_ns() {

    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

#if defined(__x86_64__) && !defined(_MSC_VER)

inline uint64_t rdtsc() {
    unsigned int lo, hi;

    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}
#else

inline uint64_t rdtsc() {
    return now_ns();
}
#endif

}
