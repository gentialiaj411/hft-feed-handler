#include <cassert>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

#include "mf/proto/nasdaq/itch50_parser.hpp"

namespace {

void put_be(std::vector<std::byte>& p, std::size_t off, std::uint64_t value, int width) {
  for (int i = 0; i < width; ++i) {
    p[off + static_cast<std::size_t>(i)] =
        std::byte{static_cast<unsigned char>((value >> ((width - 1 - i) * 8)) & 0xffU)};
  }
}

std::vector<std::byte> make_add(std::uint64_t i) {
  std::vector<std::byte> p(36);
  p[0] = std::byte{'A'};
  put_be(p, 1, 1, 2);
  put_be(p, 3, 2, 2);
  put_be(p, 5, 1'000'000 + i, 6);
  put_be(p, 11, 10'000 + i, 8);
  p[19] = (i & 1U) ? std::byte{'S'} : std::byte{'B'};
  put_be(p, 20, 100 + (i & 15U), 4);
  const char symbol[8] = {'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '};
  std::memcpy(p.data() + 24, symbol, sizeof(symbol));
  put_be(p, 32, 99'000 + (i & 31U), 4);
  return p;
}

std::vector<std::byte> make_exec(std::uint64_t i) {
  std::vector<std::byte> p(31);
  p[0] = std::byte{'E'};
  put_be(p, 1, 1, 2);
  put_be(p, 3, 2, 2);
  put_be(p, 5, 2'000'000 + i, 6);
  put_be(p, 11, 10'000 + i, 8);
  put_be(p, 19, 20 + (i & 7U), 4);
  put_be(p, 23, 70'000 + i, 8);
  return p;
}

std::vector<std::byte> make_cancel(std::uint64_t i) {
  std::vector<std::byte> p(23);
  p[0] = std::byte{'X'};
  put_be(p, 1, 1, 2);
  put_be(p, 3, 2, 2);
  put_be(p, 5, 3'000'000 + i, 6);
  put_be(p, 11, 10'000 + i, 8);
  put_be(p, 19, 10 + (i & 7U), 4);
  return p;
}

std::vector<std::byte> make_delete(std::uint64_t i) {
  std::vector<std::byte> p(19);
  p[0] = std::byte{'D'};
  put_be(p, 1, 1, 2);
  put_be(p, 3, 2, 2);
  put_be(p, 5, 4'000'000 + i, 6);
  put_be(p, 11, 10'000 + i, 8);
  return p;
}

void assert_equal(const mf::core::BookEvent& a, const mf::core::BookEvent& b) {
  assert(a.venue == b.venue);
  assert(a.type == b.type);
  assert(a.sequence == b.sequence);
  assert(a.exchange_ts_ns == b.exchange_ts_ns);
  assert(a.ingest_ts_ns == b.ingest_ts_ns);
  assert(a.symbol.bytes == b.symbol.bytes);
  assert(a.order_id == b.order_id);
  assert(a.match_id == b.match_id);
  assert(a.qty == b.qty);
  assert(a.price == b.price);
  assert(a.side == b.side);
  assert(a.raw_type == b.raw_type);
}

void test_hot_path_matches_scalar() {
  mf::proto::nasdaq::Itch50Parser parser;
  for (std::uint64_t i = 0; i < 1000; ++i) {
    std::vector<std::vector<std::byte>> payloads;
    payloads.push_back(make_add(i));
    payloads.push_back(make_exec(i));
    payloads.push_back(make_cancel(i));
    payloads.push_back(make_delete(i));
    for (const auto& payload : payloads) {
      mf::proto::nasdaq::ParseStats scalar_stats{};
      mf::proto::nasdaq::ParseStats simd_stats{};
      const auto scalar = parser.parse_message(payload, i + 1, i + 99, scalar_stats);
      const auto simd = parser.parse_hot_message_simd(payload, i + 1, i + 99, simd_stats);
      assert(scalar.has_value());
      assert(simd.has_value());
      assert_equal(*scalar, *simd);
      assert(scalar_stats.parsed_messages == simd_stats.parsed_messages);
      assert(scalar_stats.malformed_messages == simd_stats.malformed_messages);
    }
  }
}

}  // namespace

int main() {
  test_hot_path_matches_scalar();
  return 0;
}
