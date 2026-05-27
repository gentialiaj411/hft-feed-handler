#include <cassert>
#include <cstdio>

#include "mf/service/symbol_codec.hpp"

int main() {
  const auto key = mf::service::symbol_key_from_view("AAPL");
  assert(key.bytes[0] == 'A');
  assert(key.bytes[3] == 'L');
  assert(key.bytes[4] == ' ');

  const std::uint64_t u64 = mf::service::symbol_to_u64("AAPL");
  assert(u64 == key.as_u64());
  assert(mf::service::symbol_to_string(u64) == "AAPL");

  const std::uint64_t msft = mf::service::symbol_to_u64("MSFT");
  assert(mf::service::symbol_to_string(msft) == "MSFT");
  assert(msft != u64);

  std::printf("PASS replay_service_symbol\n");
  return 0;
}
