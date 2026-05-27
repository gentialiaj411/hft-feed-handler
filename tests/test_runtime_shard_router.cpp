#include <cassert>
#include <cstdint>

#include "mf/core/types.hpp"
#include "mf/runtime/shard_router.hpp"

namespace {

mf::core::SymbolKey make_symbol(const char a, const char b, const char c, const char d) {
  mf::core::SymbolKey symbol{};
  symbol.bytes = {a, b, c, d, ' ', ' ', ' ', ' '};
  return symbol;
}

void test_zero_shard_count_defaults_to_zero() {
  const auto symbol = make_symbol('A', 'A', 'P', 'L');
  assert(mf::runtime::shard_for_symbol(symbol, 0) == 0);
}

void test_symbol_and_u64_paths_agree() {
  // The SymbolKey overload and the raw-u64 overload must return the same shard
  // for matching inputs, otherwise routing diverges between the call sites in
  // the sharded pipeline.
  const auto symbol = make_symbol('M', 'S', 'F', 'T');
  const std::uint64_t v = symbol.as_u64();
  for (std::size_t shards : {1U, 2U, 4U, 8U, 16U}) {
    assert(mf::runtime::shard_for_symbol(symbol, shards) ==
           mf::runtime::shard_for_symbol_u64(v, shards));
    assert(mf::runtime::shard_for_symbol(symbol, shards) < shards);
  }
}

void test_distribution_over_multiple_symbols() {
  const mf::core::SymbolKey symbols[] = {
      make_symbol('A', 'A', 'P', 'L'),
      make_symbol('M', 'S', 'F', 'T'),
      make_symbol('N', 'V', 'D', 'A'),
      make_symbol('A', 'M', 'Z', 'N'),
      make_symbol('G', 'O', 'O', 'G'),
      make_symbol('M', 'E', 'T', 'A'),
  };

  for (std::size_t shards : {2U, 4U, 8U}) {
    bool saw_non_zero = false;
    for (const auto& symbol : symbols) {
      const auto shard = mf::runtime::shard_for_symbol(symbol, shards);
      assert(shard < shards);
      if (shard != 0) {
        saw_non_zero = true;
      }
    }
    assert(saw_non_zero);
  }
}

}  // namespace

int main() {
  test_zero_shard_count_defaults_to_zero();
  test_symbol_and_u64_paths_agree();
  test_distribution_over_multiple_symbols();
  return 0;
}
