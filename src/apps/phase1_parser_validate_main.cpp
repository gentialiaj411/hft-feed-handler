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
#include "mf/proto/cboe/pitch_parser.hpp"
#include "mf/proto/nasdaq/itch50_parser.hpp"
#include "mf/proto/nyse/pillar_parser.hpp"

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

void update_crc_from_event(std::uint32_t& crc, const mf::core::BookEvent& ev) {
  crc = mf::core::crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.venue), sizeof(ev.venue));
  crc = mf::core::crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.type), sizeof(ev.type));
  crc = mf::core::crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.sequence), sizeof(ev.sequence));
  crc = mf::core::crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.exchange_ts_ns), sizeof(ev.exchange_ts_ns));
  crc = mf::core::crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.symbol), sizeof(ev.symbol));
  crc = mf::core::crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.order_id), sizeof(ev.order_id));
  crc = mf::core::crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.qty), sizeof(ev.qty));
  crc = mf::core::crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.price), sizeof(ev.price));
}

template <typename ParserT, typename StatsT>
RunSummary run_framed_file(const std::string& path, ParserT& parser, StatsT& stats, const char* venue_name) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error(std::string("failed to open input file for ") + venue_name + ": " + path);
  }

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
      update_crc_from_event(summary.crc, *ev);
    }
  }

  summary.malformed += stats.malformed_messages;
  return summary;
}

template <typename StatsT>
void print_type_counts(const StatsT& stats) {
  for (std::size_t i = 0; i < stats.type_counts.size(); ++i) {
    if (stats.type_counts[i] == 0) continue;
    std::cout << "  " << i << ": " << stats.type_counts[i] << "\n";
  }
}

void print_summary(const char* venue_name, const RunSummary& summary) {
  std::cout << "[" << venue_name << "]\n";
  std::cout << "frames=" << summary.frames << "\n";
  std::cout << "parsed=" << summary.parsed << "\n";
  std::cout << "malformed=" << summary.malformed << "\n";
  std::cout << "crc32=0x" << std::hex << std::setw(8) << std::setfill('0') << summary.crc << std::dec << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr << "usage: phase1_parser_validate <nasdaq_itch_file> <nyse_pillar_file> <cboe_pitch_file>\n";
    return 2;
  }

  try {
    mf::proto::nasdaq::Itch50Parser nasdaq_parser;
    mf::proto::nasdaq::ParseStats nasdaq_stats;
    const RunSummary nasdaq = run_framed_file(argv[1], nasdaq_parser, nasdaq_stats, "nasdaq");
    print_summary("nasdaq", nasdaq);
    std::cout << "type_counts:\n";
    print_type_counts(nasdaq_stats);

    mf::proto::nyse::PillarParser nyse_parser;
    mf::proto::nyse::ParseStats nyse_stats;
    const RunSummary nyse = run_framed_file(argv[2], nyse_parser, nyse_stats, "nyse");
    print_summary("nyse", nyse);
    std::cout << "type_counts:\n";
    print_type_counts(nyse_stats);

    mf::proto::cboe::PitchParser cboe_parser;
    mf::proto::cboe::ParseStats cboe_stats;
    const RunSummary cboe = run_framed_file(argv[3], cboe_parser, cboe_stats, "cboe");
    print_summary("cboe", cboe);
    std::cout << "type_counts:\n";
    print_type_counts(cboe_stats);

    const std::uint32_t combined = nasdaq.crc ^ nyse.crc ^ cboe.crc;
    std::cout << "[combined]\n";
    std::cout << "crc32_xor=0x" << std::hex << std::setw(8) << std::setfill('0') << combined << std::dec << "\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << "\n";
    return 1;
  }
}
