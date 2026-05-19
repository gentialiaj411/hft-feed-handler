#pragma once

#include <cstddef>
#include <cstdint>

#include "mf/core/types.hpp"

namespace mf::runtime {

inline std::size_t shard_for_symbol_u64(std::uint64_t symbol_u64, std::size_t shard_count) noexcept {
  if (shard_count == 0) {
    return 0;
  }
  return static_cast<std::size_t>(symbol_u64 % static_cast<std::uint64_t>(shard_count));
}

inline std::size_t shard_for_symbol(const mf::core::SymbolKey& symbol, std::size_t shard_count) noexcept {
  return shard_for_symbol_u64(symbol.as_u64(), shard_count);
}

}  // namespace mf::runtime
