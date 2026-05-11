#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/proto/iex/deep_parser.hpp"

namespace {

template <typename T>
void append_le(std::vector<std::byte>& out, T v) {
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    out.push_back(static_cast<std::byte>((static_cast<std::uint64_t>(v) >> (8U * i)) & 0xFFU));
  }
}

void append_symbol(std::vector<std::byte>& out, const char* s8) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::byte>(s8[i]));
  }
}

std::vector<std::byte> make_add() {
  std::vector<std::byte> m;
  m.push_back(static_cast<std::byte>('a'));
  m.push_back(static_cast<std::byte>('8'));
  append_le<std::uint64_t>(m, 123456789ULL);
  append_symbol(m, "AAPL    ");
  append_le<std::uint64_t>(m, 42ULL);
  append_le<std::uint32_t>(m, 100U);
  append_le<std::uint64_t>(m, 991500ULL);
  return m;
}

std::vector<std::byte> make_modify() {
  std::vector<std::byte> m;
  m.push_back(static_cast<std::byte>('M'));
  m.push_back(static_cast<std::byte>(0));
  append_le<std::uint64_t>(m, 123456790ULL);
  append_symbol(m, "AAPL    ");
  append_le<std::uint64_t>(m, 42ULL);
  append_le<std::uint32_t>(m, 80U);
  append_le<std::uint64_t>(m, 991600ULL);
  return m;
}

std::vector<std::byte> make_delete() {
  std::vector<std::byte> m;
  m.push_back(static_cast<std::byte>('R'));
  m.push_back(static_cast<std::byte>(0));
  append_le<std::uint64_t>(m, 123456791ULL);
  append_symbol(m, "AAPL    ");
  append_le<std::uint64_t>(m, 42ULL);
  return m;
}

std::vector<std::byte> make_exec() {
  std::vector<std::byte> m;
  m.push_back(static_cast<std::byte>('L'));
  m.push_back(static_cast<std::byte>(0));
  append_le<std::uint64_t>(m, 123456792ULL);
  append_symbol(m, "AAPL    ");
  append_le<std::uint64_t>(m, 42ULL);
  append_le<std::uint32_t>(m, 20U);
  append_le<std::uint64_t>(m, 991700ULL);
  append_le<std::uint64_t>(m, 9001ULL);
  return m;
}

std::vector<std::byte> make_trade() {
  std::vector<std::byte> m;
  m.push_back(static_cast<std::byte>('T'));
  m.push_back(static_cast<std::byte>(0));
  append_le<std::uint64_t>(m, 123456793ULL);
  append_symbol(m, "AAPL    ");
  append_le<std::uint32_t>(m, 50U);
  append_le<std::uint64_t>(m, 991800ULL);
  append_le<std::uint64_t>(m, 9002ULL);
  return m;
}

std::vector<std::byte> make_system() {
  std::vector<std::byte> m;
  m.push_back(static_cast<std::byte>('S'));
  m.push_back(static_cast<std::byte>('O'));
  append_le<std::uint64_t>(m, 123456700ULL);
  return m;
}

std::vector<std::byte> make_security_event() {
  std::vector<std::byte> m;
  m.push_back(static_cast<std::byte>('E'));
  m.push_back(static_cast<std::byte>('O'));
  append_le<std::uint64_t>(m, 123456701ULL);
  append_symbol(m, "AAPL    ");
  return m;
}

void test_core_messages() {
  mf::proto::iex::DeepParser parser;
  mf::proto::iex::ParseStats stats{};

  auto add = parser.parse_message(make_add(), 1, 10, stats);
  assert(add.has_value());
  assert(add->type == mf::core::EventType::Add);
  assert(add->order_id == 42ULL);
  assert(add->qty == 100U);
  assert(add->price == 991500U);
  assert(add->side == mf::core::Side::Buy);

  auto mod = parser.parse_message(make_modify(), 2, 11, stats);
  assert(mod.has_value());
  assert(mod->type == mf::core::EventType::Replace);
  assert(mod->order_id == 42ULL);
  assert(mod->qty == 80U);

  auto del = parser.parse_message(make_delete(), 3, 12, stats);
  assert(del.has_value());
  assert(del->type == mf::core::EventType::Delete);
  assert(del->order_id == 42ULL);

  auto ex = parser.parse_message(make_exec(), 4, 13, stats);
  assert(ex.has_value());
  assert(ex->type == mf::core::EventType::ExecutePrice);
  assert(ex->order_id == 42ULL);
  assert(ex->qty == 20U);
  assert(ex->match_id == 9001ULL);

  auto tr = parser.parse_message(make_trade(), 5, 14, stats);
  assert(tr.has_value());
  assert(tr->type == mf::core::EventType::Trade);
  assert(tr->qty == 50U);
  assert(tr->match_id == 9002ULL);

  auto sys = parser.parse_message(make_system(), 6, 15, stats);
  assert(sys.has_value());
  assert(sys->type == mf::core::EventType::System);

  auto sev = parser.parse_message(make_security_event(), 7, 16, stats);
  assert(sev.has_value());
  assert(sev->type == mf::core::EventType::System);

  std::vector<std::byte> unknown = {static_cast<std::byte>('Z'), static_cast<std::byte>(0)};
  auto unk = parser.parse_message(unknown, 8, 17, stats);
  assert(unk.has_value());
  assert(unk->type == mf::core::EventType::Unknown);
  assert(stats.unimplemented_messages > 0);
}

}  // namespace

int main() {
  test_core_messages();
  return 0;
}
