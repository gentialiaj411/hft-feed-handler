#pragma once

#include <cstddef>
#include <cstdint>

namespace mf::core {

[[nodiscard]] std::uint32_t crc32_update(std::uint32_t seed, const std::byte* data, std::size_t length) noexcept;

}  // namespace mf::core