#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/proto/nasdaq/itch50_parser.hpp"

namespace {

void append_be_u32(std::vector<std::byte>& out, std::uint32_t v) {
  out.push_back(static_cast<std::byte>((v >> 24U) & 0xFFU));
  out.push_back(static_cast<std::byte>((v >> 16U) & 0xFFU));
  out.push_back(static_cast<std::byte>((v >> 8U) & 0xFFU));
  out.push_back(static_cast<std::byte>(v & 0xFFU));
}

void append_be_u64(std::vector<std::byte>& out, std::uint64_t v) {
  for (int i = 7; i >= 0; --i) {
    out.push_back(static_cast<std::byte>((v >> (8U * static_cast<unsigned>(i))) & 0xFFU));
  }
}

void append_ts6(std::vector<std::byte>& out, std::uint64_t ts) {
  out.push_back(static_cast<std::byte>((ts >> 40U) & 0xFFU));
  out.push_back(static_cast<std::byte>((ts >> 32U) & 0xFFU));
  out.push_back(static_cast<std::byte>((ts >> 24U) & 0xFFU));
  out.push_back(static_cast<std::byte>((ts >> 16U) & 0xFFU));
  out.push_back(static_cast<std::byte>((ts >> 8U) & 0xFFU));
  out.push_back(static_cast<std::byte>(ts & 0xFFU));
}

void append_symbol8(std::vector<std::byte>& out, const char* sym8) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::byte>(sym8[i]));
  }
}

std::vector<std::byte> make_add_message() {
  // ITCH A message wire payload (includes type byte).
  std::vector<std::byte> m;
  m.push_back(static_cast<std::byte>('A'));
  m.push_back(static_cast<std::byte>(0));  // stock locate high
  m.push_back(static_cast<std::byte>(1));  // stock locate low
  m.push_back(static_cast<std::byte>(0));  // tracking high
  m.push_back(static_cast<std::byte>(2));  // tracking low
  append_ts6(m, 123456789ULL);
  append_be_u64(m, 42ULL);                 // order_ref
  m.push_back(static_cast<std::byte>('B'));// buy/sell
  append_be_u32(m, 100U);                  // shares
  append_symbol8(m, "AAPL    ");           // stock
  append_be_u32(m, 991500U);               // price
  return m;
}

std::vector<std::byte> make_system_message() {
  std::vector<std::byte> m;
  m.push_back(static_cast<std::byte>('S'));
  m.push_back(static_cast<std::byte>(0));
  m.push_back(static_cast<std::byte>(1));
  m.push_back(static_cast<std::byte>(0));
  m.push_back(static_cast<std::byte>(2));
  append_ts6(m, 123450000ULL);
  m.push_back(static_cast<std::byte>('O'));
  return m;
}

void test_core_messages() {
  mf::proto::nasdaq::Itch50Parser parser;
  mf::proto::nasdaq::ParseStats stats{};

  auto add = parser.parse_message(make_add_message(), 1, 10, stats);
  assert(add.has_value());
  assert(add->venue == mf::core::Venue::Nasdaq);
  assert(add->type == mf::core::EventType::Add);
  assert(add->sequence == 1);
  assert(add->order_id == 42ULL);
  assert(add->qty == 100U);
  assert(add->price == 991500U);
  assert(add->side == mf::core::Side::Buy);

  auto sys = parser.parse_message(make_system_message(), 2, 11, stats);
  assert(sys.has_value());
  assert(sys->type == mf::core::EventType::System);
  assert(sys->sequence == 2);
}

}  // namespace

int main() {
  test_core_messages();
  return 0;
}
