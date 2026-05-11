#pragma once

#include <ctime>
#include <cstdint>

namespace mf::core {

[[nodiscard]] std::uint64_t monotonic_raw_now_ns() noexcept;

}  // namespace mf::core