#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "mf/core/types.hpp"

namespace mf::service {

// Encode a human symbol (e.g. "AAPL") into the 8-byte ITCH-style key used in journals.
inline mf::core::SymbolKey symbol_key_from_view(std::string_view symbol) noexcept {
  mf::core::SymbolKey key{};
  for (std::size_t i = 0; i < key.bytes.size(); ++i) {
    key.bytes[i] = (i < symbol.size()) ? symbol[i] : ' ';
  }
  return key;
}

inline std::uint64_t symbol_to_u64(std::string_view symbol) noexcept {
  return symbol_key_from_view(symbol).as_u64();
}

inline std::string symbol_to_string(std::uint64_t symbol_u64) {
  mf::core::SymbolKey key{};
  for (std::size_t i = 0; i < key.bytes.size(); ++i) {
    key.bytes[i] = static_cast<char>((symbol_u64 >> (56 - 8 * i)) & 0xFF);
  }
  std::string out(key.bytes.begin(), key.bytes.end());
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

}  // namespace mf::service
