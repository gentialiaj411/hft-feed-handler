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
#include "mf/phase2/pipeline.hpp"
#include "mf/proto/cboe/pitch_parser.hpp"
#include "mf/proto/iex/deep_parser.hpp"
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

template <typename IexParserT, typename IexStatsT>
RunSummary run_iex_pcap_file(const std::string& path, IexParserT& parser, IexStatsT& stats) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("failed to open iex pcap file: " + path);
  }

  // libpcap global header
  std::array<std::uint8_t, 24> gh{};
  if (!in.read(reinterpret_cast<char*>(gh.data()), static_cast<std::streamsize>(gh.size()))) {
    throw std::runtime_error("pcap global header read failure: " + path);
  }

  const std::uint32_t magic_le = static_cast<std::uint32_t>(gh[0]) |
                                 (static_cast<std::uint32_t>(gh[1]) << 8U) |
                                 (static_cast<std::uint32_t>(gh[2]) << 16U) |
                                 (static_cast<std::uint32_t>(gh[3]) << 24U);
  if (magic_le != 0xa1b2c3d4U && magic_le != 0xd4c3b2a1U) {
    throw std::runtime_error("unsupported pcap magic value");
  }

  RunSummary summary;
  std::uint64_t seq = 1;

  while (true) {
    std::array<std::uint8_t, 16> ph{};
    if (!in.read(reinterpret_cast<char*>(ph.data()), static_cast<std::streamsize>(ph.size()))) {
      break;
    }

    const std::uint32_t incl_len = static_cast<std::uint32_t>(ph[8]) |
                                   (static_cast<std::uint32_t>(ph[9]) << 8U) |
                                   (static_cast<std::uint32_t>(ph[10]) << 16U) |
                                   (static_cast<std::uint32_t>(ph[11]) << 24U);
    if (incl_len == 0U) {
      ++summary.malformed;
      continue;
    }

    std::vector<std::uint8_t> pkt(incl_len);
    if (!in.read(reinterpret_cast<char*>(pkt.data()), static_cast<std::streamsize>(incl_len))) {
      break;
    }

    // Ethernet (14) + IPv4 + UDP expected.
    if (pkt.size() < 42) {
      ++summary.malformed;
      continue;
    }
    if (!(pkt[12] == 0x08 && pkt[13] == 0x00)) {  // IPv4 Ethertype
      continue;
    }
    const std::size_t ip_off = 14;
    const std::uint8_t ihl_words = static_cast<std::uint8_t>(pkt[ip_off] & 0x0FU);
    const std::size_t ip_hlen = static_cast<std::size_t>(ihl_words) * 4U;
    if (ip_hlen < 20U || pkt.size() < ip_off + ip_hlen + 8U) {
      ++summary.malformed;
      continue;
    }
    if (pkt[ip_off + 9] != 17U) {  // UDP
      continue;
    }

    const std::size_t udp_off = ip_off + ip_hlen;
    const std::uint16_t udp_len = static_cast<std::uint16_t>((static_cast<std::uint16_t>(pkt[udp_off + 4]) << 8U) |
                                                              static_cast<std::uint16_t>(pkt[udp_off + 5]));
    if (udp_len < 8U) {
      ++summary.malformed;
      continue;
    }
    const std::size_t udp_payload_len = static_cast<std::size_t>(udp_len) - 8U;
    if (pkt.size() < udp_off + 8U + udp_payload_len) {
      ++summary.malformed;
      continue;
    }

    const std::uint8_t* payload = pkt.data() + udp_off + 8U;
    if (udp_payload_len < 40U) {
      ++summary.malformed;
      continue;
    }

    // IEX-TP UDP payload begins with a 40-byte transport/session header.
    std::size_t off = 40U;
    while (off + 2U <= udp_payload_len) {
      const std::uint16_t msg_len = static_cast<std::uint16_t>(payload[off]) |
                                    static_cast<std::uint16_t>(payload[off + 1] << 8U);
      off += 2U;
      if (msg_len == 0U || off + msg_len > udp_payload_len) {
        ++summary.malformed;
        break;
      }

      ++summary.frames;
      auto ev = parser.parse_message(
          std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload + off), msg_len),
          seq++,
          mf::core::monotonic_raw_now_ns(),
          stats);
      if (ev.has_value()) {
        ++summary.parsed;
        update_crc_from_event(summary.crc, *ev);
      }
      off += msg_len;
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

template <typename ParserT, typename StatsT>
RunSummary run_framed_file_phase2(
    const std::string& path,
    ParserT& parser,
    StatsT& stats,
    const char* venue_name,
    mf::phase2::Pipeline& pipeline) {
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
    if (!ev.has_value()) {
      continue;
    }
    ++summary.parsed;
    update_crc_from_event(summary.crc, *ev);
    pipeline.on_event(*ev);
  }

  summary.malformed += stats.malformed_messages;
  return summary;
}

template <typename IexParserT, typename IexStatsT>
RunSummary run_iex_pcap_file_phase2(
    const std::string& path,
    IexParserT& parser,
    IexStatsT& stats,
    mf::phase2::Pipeline& pipeline) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("failed to open iex pcap file: " + path);
  }

  std::array<std::uint8_t, 24> gh{};
  if (!in.read(reinterpret_cast<char*>(gh.data()), static_cast<std::streamsize>(gh.size()))) {
    throw std::runtime_error("pcap global header read failure: " + path);
  }

  const std::uint32_t magic_le = static_cast<std::uint32_t>(gh[0]) |
                                 (static_cast<std::uint32_t>(gh[1]) << 8U) |
                                 (static_cast<std::uint32_t>(gh[2]) << 16U) |
                                 (static_cast<std::uint32_t>(gh[3]) << 24U);
  if (magic_le != 0xa1b2c3d4U && magic_le != 0xd4c3b2a1U) {
    throw std::runtime_error("unsupported pcap magic value");
  }

  RunSummary summary;
  std::uint64_t seq = 1;

  while (true) {
    std::array<std::uint8_t, 16> ph{};
    if (!in.read(reinterpret_cast<char*>(ph.data()), static_cast<std::streamsize>(ph.size()))) {
      break;
    }
    const std::uint32_t incl_len = static_cast<std::uint32_t>(ph[8]) |
                                   (static_cast<std::uint32_t>(ph[9]) << 8U) |
                                   (static_cast<std::uint32_t>(ph[10]) << 16U) |
                                   (static_cast<std::uint32_t>(ph[11]) << 24U);
    if (incl_len == 0U) {
      ++summary.malformed;
      continue;
    }

    std::vector<std::uint8_t> pkt(incl_len);
    if (!in.read(reinterpret_cast<char*>(pkt.data()), static_cast<std::streamsize>(incl_len))) {
      break;
    }
    if (pkt.size() < 42 || !(pkt[12] == 0x08 && pkt[13] == 0x00)) continue;

    const std::size_t ip_off = 14;
    const std::size_t ip_hlen = static_cast<std::size_t>(pkt[ip_off] & 0x0FU) * 4U;
    if (ip_hlen < 20U || pkt.size() < ip_off + ip_hlen + 8U || pkt[ip_off + 9] != 17U) continue;

    const std::size_t udp_off = ip_off + ip_hlen;
    const std::uint16_t udp_len = static_cast<std::uint16_t>((static_cast<std::uint16_t>(pkt[udp_off + 4]) << 8U) |
                                                              static_cast<std::uint16_t>(pkt[udp_off + 5]));
    if (udp_len < 8U) {
      ++summary.malformed;
      continue;
    }
    const std::size_t udp_payload_len = static_cast<std::size_t>(udp_len) - 8U;
    if (pkt.size() < udp_off + 8U + udp_payload_len || udp_payload_len < 40U) {
      ++summary.malformed;
      continue;
    }

    const std::uint8_t* payload = pkt.data() + udp_off + 8U;
    std::size_t off = 40U;
    while (off + 2U <= udp_payload_len) {
      const std::uint16_t msg_len = static_cast<std::uint16_t>(payload[off]) |
                                    static_cast<std::uint16_t>(payload[off + 1] << 8U);
      off += 2U;
      if (msg_len == 0U || off + msg_len > udp_payload_len) {
        ++summary.malformed;
        break;
      }

      ++summary.frames;
      auto ev = parser.parse_message(
          std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload + off), msg_len),
          seq++,
          mf::core::monotonic_raw_now_ns(),
          stats);
      if (ev.has_value()) {
        ++summary.parsed;
        update_crc_from_event(summary.crc, *ev);
        pipeline.on_event(*ev);
      }
      off += msg_len;
    }
  }

  summary.malformed += stats.malformed_messages;
  return summary;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 4 && std::string(argv[1]) == "--phase2-merge") {
    try {
      if (looks_like_text_csv(argv[2])) {
        throw std::runtime_error("nasdaq input appears to be text/CSV, expected framed binary wire messages");
      }
      if (looks_like_text_csv(argv[3])) {
        throw std::runtime_error("iex input appears to be text/CSV, expected pcap-derived binary");
      }

      mf::proto::nasdaq::Itch50Parser nasdaq_parser;
      mf::proto::nasdaq::ParseStats nasdaq_stats;
      mf::proto::iex::DeepParser iex_parser;
      mf::proto::iex::ParseStats iex_stats;
      mf::phase2::Pipeline pipeline(/*gap_window=*/256, /*per_venue_capacity=*/1U << 20U);

      const RunSummary nasdaq = run_framed_file_phase2(argv[2], nasdaq_parser, nasdaq_stats, "nasdaq", pipeline);
      const RunSummary iex =
          run_iex_pcap_file_phase2(argv[3], iex_parser, iex_stats, pipeline);

      pipeline.finalize();
      const auto& phase2 = pipeline.stats();

      print_summary("nasdaq", nasdaq);
      std::cout << "type_counts:\n";
      print_type_counts(nasdaq_stats);
      print_summary("iex", iex);
      std::cout << "type_counts:\n";
      print_type_counts(iex_stats);
      std::cout << "[phase2]\n";
      std::cout << "accepted=" << phase2.accepted << "\n";
      std::cout << "dropped_duplicate_or_old=" << phase2.dropped_duplicate_or_old << "\n";
      std::cout << "buffered_out_of_order=" << phase2.buffered_out_of_order << "\n";
      std::cout << "dropped_gap_too_large=" << phase2.dropped_gap_too_large << "\n";
      std::cout << "recovery_requests=" << phase2.recovery_requests << "\n";
      std::cout << "recovery_reinjected=" << phase2.recovery_reinjected << "\n";
      std::cout << "merged_crc32=0x" << std::hex << std::setw(8) << std::setfill('0') << phase2.merged_crc << std::dec
                << "\n";
      return 0;
    } catch (const std::exception& ex) {
      std::cerr << ex.what() << "\n";
      return 1;
    }
  }

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
  if (argc == 3 && std::string(argv[1]) == "--iex-only") {
    try {
      if (looks_like_text_csv(argv[2])) {
        throw std::runtime_error("iex input appears to be text/CSV, expected pcap-derived binary");
      }
      mf::proto::iex::DeepParser iex_parser;
      mf::proto::iex::ParseStats iex_stats;
      const RunSummary iex = run_iex_pcap_file(argv[2], iex_parser, iex_stats);
      print_summary("iex", iex);
      std::cout << "type_counts:\n";
      print_type_counts(iex_stats);
      std::cout << "[combined]\n";
      std::cout << "crc32_xor=0x" << std::hex << std::setw(8) << std::setfill('0') << iex.crc << std::dec << "\n";
      return 0;
    } catch (const std::exception& ex) {
      std::cerr << ex.what() << "\n";
      return 1;
    }
  }

  if (argc != 3 && argc != 4) {
    std::cerr << "usage: phase1_parser_validate <nasdaq_itch_file> <iex_deep_file> [cboe_pitch_file]\n";
    std::cerr << "   or: phase1_parser_validate --phase2-merge <nasdaq_itch_file> <iex_pcap_file>\n";
    std::cerr << "   or: phase1_parser_validate --nasdaq-only <nasdaq_itch_file>\n";
    std::cerr << "   or: phase1_parser_validate --iex-only <iex_pcap_file>\n";
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
    const RunSummary iex = run_iex_pcap_file(argv[2], iex_parser, iex_stats);
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
