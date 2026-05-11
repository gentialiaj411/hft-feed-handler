#include "mf/core/crc32.hpp"

namespace mf::core {

std::uint32_t crc32_update(std::uint32_t seed, const std::byte* data, std::size_t length) noexcept {
  std::uint32_t crc = ~seed;
  for (std::size_t i = 0; i < length; ++i) {
    crc ^= static_cast<std::uint8_t>(data[i]);
    for (int bit = 0; bit < 8; ++bit) {
      const std::uint32_t mask = static_cast<std::uint32_t>(-(static_cast<std::int32_t>(crc & 1U)));
      crc = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
  }
  return ~crc;
}

}  // namespace mf::core