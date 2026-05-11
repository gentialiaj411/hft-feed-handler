#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "mf/core/time.hpp"
#include "mf/proto/nasdaq/itch50_parser.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size == 0) {
    return 0;
  }

  std::vector<std::byte> payload(size);
  for (std::size_t i = 0; i < size; ++i) {
    payload[i] = static_cast<std::byte>(data[i]);
  }

  mf::proto::nasdaq::Itch50Parser parser;
  mf::proto::nasdaq::ParseStats stats;
  (void)parser.parse_message(std::span<const std::byte>(payload.data(), payload.size()), 1, mf::core::monotonic_raw_now_ns(), stats);
  return 0;
}