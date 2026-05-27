#pragma once

#include <cstddef>
#include <cstdint>

#include "mf/core/types.hpp"

namespace mf::runtime {

// SymbolKey is a left-padded 8-byte ASCII ticker padded on the right with
// spaces (0x20). Taking `value % shard_count` on the raw u64 picks up only
// the low bits, which for tickers shorter than 8 chars are always 0x20 and
// therefore collapse to shard 0 for any power-of-two shard count. We mix
// the bits with a small splitmix64 finalizer before applying the modulo so
// the distribution depends on every byte of the symbol.
inline constexpr std::uint64_t splitmix64_mix(std::uint64_t v) noexcept {
  v ^= v >> 30;
  v *= 0xbf58476d1ce4e5b9ULL;
  v ^= v >> 27;
  v *= 0x94d049bb133111ebULL;
  v ^= v >> 31;
  return v;
}

inline std::size_t shard_for_symbol_u64(std::uint64_t symbol_u64, std::size_t shard_count) noexcept {
  if (shard_count == 0) {
    return 0;
  }
  const std::uint64_t mixed = splitmix64_mix(symbol_u64);
  return static_cast<std::size_t>(mixed % static_cast<std::uint64_t>(shard_count));
}

inline std::size_t shard_for_symbol(const mf::core::SymbolKey& symbol, std::size_t shard_count) noexcept {
  return shard_for_symbol_u64(symbol.as_u64(), shard_count);
}

}  // namespace mf::runtime
