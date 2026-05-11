#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "mf/core/crc32.hpp"
#include "mf/core/time.hpp"
#include "mf/proto/nasdaq/itch50_parser.hpp"

namespace {

struct RunSummary {
  std::uint64_t frames{0};
  std::uint64_t parsed{0};
  std::uint64_t malformed{0};
  std::uint32_t crc{0};
};

std::uint16_t read_u16_be(const std::uint8_t* p) {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8U) |
                                    static_cast<std::uint16_t>(p[1]));
}

RunSummary run_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("failed to open input file: " + path);
  }

  mf::proto::nasdaq::Itch50Parser parser;
  mf::proto::nasdaq::ParseStats stats;
  RunSummary summary;

  std::array<std::uint8_t, 2> len_buf{};
  std::uint64_t seq = 1;

  while (in.read(reinterpret_cast<char*>(len_buf.data()), len_buf.size())) {
    const std::uint16_t msg_len = read_u16_be(len_buf.data());
    if (msg_len == 0) {
      ++summary.malformed;
      continue;
    }

    std::vector<std::byte> msg(msg_len);
    if (!in.read(reinterpret_cast<char*>(msg.data()), static_cast<std::streamsize>(msg_len))) {
      break;
    }

    ++summary.frames;

    auto ev = parser.parse_message(
        std::span<const std::byte>(msg.data(), msg.size()),
        seq++,
        mf::core::monotonic_raw_now_ns(),
        stats);

    if (ev.has_value()) {
      ++summary.parsed;
      summary.crc = mf::core::crc32_update(summary.crc, reinterpret_cast<const std::byte*>(&ev->venue), sizeof(ev->venue));
      summary.crc = mf::core::crc32_update(summary.crc, reinterpret_cast<const std::byte*>(&ev->type), sizeof(ev->type));
      summary.crc = mf::core::crc32_update(summary.crc, reinterpret_cast<const std::byte*>(&ev->sequence), sizeof(ev->sequence));
      summary.crc = mf::core::crc32_update(summary.crc, reinterpret_cast<const std::byte*>(&ev->exchange_ts_ns), sizeof(ev->exchange_ts_ns));
      summary.crc = mf::core::crc32_update(summary.crc, reinterpret_cast<const std::byte*>(&ev->symbol), sizeof(ev->symbol));
      summary.crc = mf::core::crc32_update(summary.crc, reinterpret_cast<const std::byte*>(&ev->order_id), sizeof(ev->order_id));
      summary.crc = mf::core::crc32_update(summary.crc, reinterpret_cast<const std::byte*>(&ev->qty), sizeof(ev->qty));
      summary.crc = mf::core::crc32_update(summary.crc, reinterpret_cast<const std::byte*>(&ev->price), sizeof(ev->price));
    }
  }

  summary.malformed += stats.malformed_messages;

  std::cout << "frames=" << summary.frames << "\n";
  std::cout << "parsed=" << summary.parsed << "\n";
  std::cout << "malformed=" << summary.malformed << "\n";
  std::cout << "crc32=0x" << std::hex << std::setw(8) << std::setfill('0') << summary.crc << std::dec << "\n";

  std::cout << "type_counts:\n";
  for (std::size_t i = 0; i < stats.type_counts.size(); ++i) {
    if (stats.type_counts[i] == 0) continue;
    std::cout << "  " << static_cast<char>(i) << "(" << i << "): " << stats.type_counts[i] << "\n";
  }

  return summary;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: phase1_parser_validate <nasdaq_itch_file>\n";
    return 2;
  }

  try {
    (void)run_file(argv[1]);
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << "\n";
    return 1;
  }
}