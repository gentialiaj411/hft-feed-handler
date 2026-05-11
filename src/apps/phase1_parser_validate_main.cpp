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
#include "mf/proto/iex/deep_parser.hpp"

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

bool looks_like_text_csv(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::array<unsigned char, 4096> buf{};
  in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
  const std::streamsize n = in.gcount();
  if (n <= 0) return false;

  std::size_t printable = 0;
  std::size_t commas = 0;
  std::size_t newlines = 0;
  for (std::streamsize i = 0; i < n; ++i) {
    const unsigned char b = buf[static_cast<std::size_t>(i)];
    if ((b >= 32 && b <= 126) || b == '\n' || b == '\r' || b == '\t') {
      ++printable;
    }
    if (b == ',') ++commas;
    if (b == '\n') ++newlines;
  }

  const double printable_ratio = static_cast<double>(printable) / static_cast<double>(n);
  return printable_ratio > 0.95 && commas > 10 && newlines > 5;
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
  if (argc == 3 && std::string(argv[1]) == "--nasdaq-only") {
    try {
      if (looks_like_text_csv(argv[2])) {
        throw std::runtime_error("nasdaq input appears to be text/CSV, expected framed binary wire messages");
      }
      mf::proto::nasdaq::Itch50Parser nasdaq_parser;
      mf::proto::nasdaq::ParseStats nasdaq_stats;
      const RunSummary nasdaq = run_framed_file(argv[2], nasdaq_parser, nasdaq_stats, "nasdaq");
      print_summary("nasdaq", nasdaq);
      std::cout << "type_counts:\n";
      print_type_counts(nasdaq_stats);
      std::cout << "[combined]\n";
      std::cout << "crc32_xor=0x" << std::hex << std::setw(8) << std::setfill('0') << nasdaq.crc << std::dec << "\n";
      return 0;
    } catch (const std::exception& ex) {
      std::cerr << ex.what() << "\n";
      return 1;
    }
  }

  if (argc != 3 && argc != 4) {
    std::cerr << "usage: phase1_parser_validate <nasdaq_itch_file> <iex_deep_file> [cboe_pitch_file]\n";
    std::cerr << "   or: phase1_parser_validate --nasdaq-only <nasdaq_itch_file>\n";
    return 2;
  }

  try {
    if (looks_like_text_csv(argv[1])) {
      throw std::runtime_error("nasdaq input appears to be text/CSV, expected framed binary wire messages");
    }
    if (looks_like_text_csv(argv[2])) {
      throw std::runtime_error("iex input appears to be text/CSV, expected framed binary wire messages");
    }
    if (argc == 4 && looks_like_text_csv(argv[3])) {
      throw std::runtime_error("cboe input appears to be text/CSV, expected framed binary wire messages");
    }

    mf::proto::nasdaq::Itch50Parser nasdaq_parser;
    mf::proto::nasdaq::ParseStats nasdaq_stats;
    const RunSummary nasdaq = run_framed_file(argv[1], nasdaq_parser, nasdaq_stats, "nasdaq");
    print_summary("nasdaq", nasdaq);
    std::cout << "type_counts:\n";
    print_type_counts(nasdaq_stats);

    mf::proto::iex::DeepParser iex_parser;
    mf::proto::iex::ParseStats iex_stats;
    const RunSummary iex = run_framed_file(argv[2], iex_parser, iex_stats, "iex");
    print_summary("iex", iex);
    std::cout << "type_counts:\n";
    print_type_counts(iex_stats);

    std::uint32_t combined = nasdaq.crc ^ iex.crc;
    if (argc == 4) {
      mf::proto::cboe::PitchParser cboe_parser;
      mf::proto::cboe::ParseStats cboe_stats;
      const RunSummary cboe = run_framed_file(argv[3], cboe_parser, cboe_stats, "cboe");
      print_summary("cboe", cboe);
      std::cout << "type_counts:\n";
      print_type_counts(cboe_stats);
      combined ^= cboe.crc;
    } else {
      std::cout << "[cboe]\n";
      std::cout << "skipped (no input file provided)\n";
    }

    std::cout << "[combined]\n";
    std::cout << "crc32_xor=0x" << std::hex << std::setw(8) << std::setfill('0') << combined << std::dec << "\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << "\n";
    return 1;
  }
}
