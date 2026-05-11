#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace mf::proto {

template <typename T>
[[nodiscard]] inline T read_be(const std::byte* ptr) noexcept {
  static_assert(std::is_integral_v<T>, "read_be<T> only supports integral types");

  T value{};
  std::memcpy(&value, ptr, sizeof(T));

  if constexpr (sizeof(T) == 1) {
    return value;
  } else if constexpr (sizeof(T) == 2) {
    auto v = static_cast<std::uint16_t>(value);
#if defined(_MSC_VER)
    return static_cast<T>(_byteswap_ushort(v));
#else
    return static_cast<T>(__builtin_bswap16(v));
#endif
  } else if constexpr (sizeof(T) == 4) {
    auto v = static_cast<std::uint32_t>(value);
#if defined(_MSC_VER)
    return static_cast<T>(_byteswap_ulong(v));
#else
    return static_cast<T>(__builtin_bswap32(v));
#endif
  } else if constexpr (sizeof(T) == 8) {
    auto v = static_cast<std::uint64_t>(value);
#if defined(_MSC_VER)
    return static_cast<T>(_byteswap_uint64(v));
#else
    return static_cast<T>(__builtin_bswap64(v));
#endif
  } else {
    static_assert(sizeof(T) <= 8, "unsupported integer width in read_be<T>");
    return value;
  }
}

}  // namespace mf::proto
