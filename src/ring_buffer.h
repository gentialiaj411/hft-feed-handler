#pragma once

#include <cstddef>

#include "mf/core/spsc_ring.hpp"

namespace market {

template <typename T, std::size_t Size>
using SPSCRingBuffer = mf::core::SPSCRingBuffer<T, Size>;

}  // namespace market
