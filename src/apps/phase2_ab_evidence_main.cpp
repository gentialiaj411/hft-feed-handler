#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "mf/core/time.hpp"
#include "mf/core/types.hpp"
#include "mf/phase2/ab_arbiter.hpp"
#include "mf/phase2/pipeline.hpp"
#include "mf/proto/cboe/pitch_parser.hpp"
#include "mf/proto/iex/deep_parser.hpp"
#include "mf/proto/nasdaq/itch50_messages.hpp"
#include "mf/proto/nasdaq/itch50_parser.hpp"

namespace {

// ── File-loading helpers ──────────────────────────────────────────────────

static std::uint16_t read_u16_be(const std::uint8_t* p) {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8U) |
                                    static_cast<std::uint16_t>(p[1]));
}

static std::size_t nasdaq_itch_wire_len(char t) {
  using namespace mf::proto::nasdaq;
  switch (t) {
    case 'A': return sizeof(AddOrderMessage) + 1U;
    case 'F': return sizeof(AddOrderMpidMessage) + 1U;
    case 'E': return sizeof(OrderExecutedMessage) + 1U;
    case 'C': return sizeof(OrderExecutedPriceMessage) + 1U;
    case 'X': return sizeof(OrderCancelMessage) + 1U;
    case 'D': return sizeof(OrderDeleteMessage) + 1U;
    case 'U': return sizeof(OrderReplaceMessage) + 1U;
    case 'P': return sizeof(TradeMessage) + 1U;
    case 'Q': return sizeof(CrossTradeMessage) + 1U;
    case 'I': return sizeof(NoiiMessage) + 1U;
    case 'S': return sizeof(SystemEventMessage) + 1U;
    case 'R': return sizeof(StockDirectoryMessage) + 1U;
    default:  return 0U;
  }
}

static void load_nasdaq_framed(const std::string& path, std::vector<mf::core::BookEvent>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open nasdaq file: " + path);
  mf::proto::nasdaq::Itch50Parser parser;
  mf::proto::nasdaq::ParseStats stats;
  std::array<std::uint8_t, 2> lbuf{};
  std::uint64_t seq = 1;
  while (in.read(reinterpret_cast<char*>(lbuf.data()), 2)) {
    const std::uint16_t len = read_u16_be(lbuf.data());
    if (len == 0) continue;
    std::vector<std::byte> msg(len);
    if (!in.read(reinterpret_cast<char*>(msg.data()), len)) break;
    auto ev = parser.parse_message(std::span<const std::byte>(msg), seq++,
                                   mf::core::monotonic_raw_now_ns(), stats);
    if (ev) out.push_back(*ev);
  }
}

static void load_nasdaq_raw_itch(const std::string& path, std::vector<mf::core::BookEvent>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open nasdaq raw itch file: " + path);
  mf::proto::nasdaq::Itch50Parser parser;
  mf::proto::nasdaq::ParseStats stats;
  std::uint64_t seq = 1;
  while (true) {
    char t = '\0';
    if (!in.read(&t, 1)) break;
    const std::size_t len = nasdaq_itch_wire_len(t);
    if (len == 0U)
      throw std::runtime_error(std::string("unknown ITCH type: ") + t);
    std::vector<std::byte> msg(len);
    msg[0] = static_cast<std::byte>(t);
    if (!in.read(reinterpret_cast<char*>(msg.data() + 1), static_cast<std::streamsize>(len - 1U))) break;
    auto ev = parser.parse_message(std::span<const std::byte>(msg), seq++,
                                   mf::core::monotonic_raw_now_ns(), stats);
    if (ev) out.push_back(*ev);
  }
}

static void load_iex_pcap(const std::string& path, std::vector<mf::core::BookEvent>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open iex pcap file: " + path);
  std::array<std::uint8_t, 24> gh{};
  if (!in.read(reinterpret_cast<char*>(gh.data()), 24))
    throw std::runtime_error("pcap global header failure: " + path);
  const std::uint32_t magic = static_cast<std::uint32_t>(gh[0]) |
                              (static_cast<std::uint32_t>(gh[1]) << 8U) |
                              (static_cast<std::uint32_t>(gh[2]) << 16U) |
                              (static_cast<std::uint32_t>(gh[3]) << 24U);
  if (magic != 0xa1b2c3d4U && magic != 0xd4c3b2a1U)
    throw std::runtime_error("unsupported pcap magic");

  mf::proto::iex::DeepParser parser;
  mf::proto::iex::ParseStats stats;
  std::uint64_t seq = 1;
  while (true) {
    std::array<std::uint8_t, 16> ph{};
    if (!in.read(reinterpret_cast<char*>(ph.data()), 16)) break;
    const std::uint32_t incl = static_cast<std::uint32_t>(ph[8]) | (static_cast<std::uint32_t>(ph[9]) << 8U) |
                               (static_cast<std::uint32_t>(ph[10]) << 16U) | (static_cast<std::uint32_t>(ph[11]) << 24U);
    if (incl == 0U || incl > 65535U) { in.seekg(incl, std::ios::cur); continue; }
    std::vector<std::uint8_t> pkt(incl);
    if (!in.read(reinterpret_cast<char*>(pkt.data()), incl)) break;
    if (pkt.size() < 42 || !(pkt[12] == 0x08 && pkt[13] == 0x00)) continue;
    const std::size_t ip_off = 14;
    const std::size_t ip_hlen = static_cast<std::size_t>(pkt[ip_off] & 0x0FU) * 4U;
    if (ip_hlen < 20U || pkt.size() < ip_off + ip_hlen + 8U || pkt[ip_off + 9] != 17U) continue;
    const std::size_t udp_off = ip_off + ip_hlen;
    const std::size_t udp_pl = static_cast<std::size_t>(
        (static_cast<std::uint16_t>(pkt[udp_off + 4]) << 8U) | pkt[udp_off + 5]) - 8U;
    if (pkt.size() < udp_off + 8U + udp_pl || udp_pl < 40U) continue;
    const std::uint8_t* pl = pkt.data() + udp_off + 8U;
    std::size_t off = 40U;
    while (off + 2U <= udp_pl) {
      const std::uint16_t mlen = static_cast<std::uint16_t>(pl[off]) | static_cast<std::uint16_t>(pl[off + 1] << 8U);
      off += 2U;
      if (mlen == 0U || off + mlen > udp_pl) break;
      auto ev = parser.parse_message(
          std::span<const std::byte>(reinterpret_cast<const std::byte*>(pl + off), mlen),
          seq++, mf::core::monotonic_raw_now_ns(), stats);
      if (ev) out.push_back(*ev);
      off += mlen;
    }
  }
}

static void load_cboe_framed(const std::string& path, std::vector<mf::core::BookEvent>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open cboe file: " + path);
  mf::proto::cboe::PitchParser parser;
  mf::proto::cboe::ParseStats stats;
  std::array<std::uint8_t, 2> lbuf{};
  std::uint64_t seq = 1;
  while (in.read(reinterpret_cast<char*>(lbuf.data()), 2)) {
    const std::uint16_t len = read_u16_be(lbuf.data());
    if (len == 0) continue;
    std::vector<std::byte> msg(len);
    if (!in.read(reinterpret_cast<char*>(msg.data()), len)) break;
    auto ev = parser.parse_message(std::span<const std::byte>(msg), seq++,
                                   mf::core::monotonic_raw_now_ns(), stats);
    if (ev) out.push_back(*ev);
  }
}

// ── Config & CLI ──────────────────────────────────────────────────────────

struct Config {
  std::size_t events{500000};
  std::uint64_t gap_window{256};
  std::size_t capacity{1U << 20U};
  double drop_a{0.02};
  double drop_b{0.02};
  std::uint64_t seed{11};
  bool complementary_drops{false};
  // real-file mode
  bool from_files{false};
  bool nasdaq_raw_itch{false};
  std::string nasdaq_path{};
  std::string iex_path{};
  std::string cboe_path{};
};

Config parse_args(int argc, char** argv) {
  Config cfg{};
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--events" && i + 1 < argc) {
      cfg.events = static_cast<std::size_t>(std::stoull(argv[++i]));
    } else if (a == "--gap-window" && i + 1 < argc) {
      cfg.gap_window = static_cast<std::uint64_t>(std::stoull(argv[++i]));
    } else if (a == "--capacity" && i + 1 < argc) {
      cfg.capacity = static_cast<std::size_t>(std::stoull(argv[++i]));
    } else if (a == "--drop-a" && i + 1 < argc) {
      cfg.drop_a = std::stod(argv[++i]);
    } else if (a == "--drop-b" && i + 1 < argc) {
      cfg.drop_b = std::stod(argv[++i]);
    } else if (a == "--seed" && i + 1 < argc) {
      cfg.seed = static_cast<std::uint64_t>(std::stoull(argv[++i]));
    } else if (a == "--complementary-drops") {
      cfg.complementary_drops = true;
    } else if (a == "--from-files") {
      cfg.from_files = true;
    } else if (a == "--nasdaq-raw-itch") {
      cfg.nasdaq_raw_itch = true;
    } else if (a == "--nasdaq" && i + 1 < argc) {
      cfg.nasdaq_path = argv[++i];
    } else if (a == "--iex" && i + 1 < argc) {
      cfg.iex_path = argv[++i];
    } else if (a == "--cboe" && i + 1 < argc) {
      cfg.cboe_path = argv[++i];
    } else if (a == "--help" || a == "-h") {
      std::cout << "Synthetic mode:\n";
      std::cout << "  phase2_ab_evidence [--events N] [--gap-window N] [--capacity N]\n";
      std::cout << "                     [--drop-a R] [--drop-b R] [--seed N] [--complementary-drops]\n";
      std::cout << "Real-file mode:\n";
      std::cout << "  phase2_ab_evidence --from-files --nasdaq <file> --iex <pcap>\n";
      std::cout << "                     [--cboe <file>] [--nasdaq-raw-itch]\n";
      std::cout << "                     [--drop-a R] [--drop-b R] [--seed N] [--gap-window N]\n";
      std::exit(0);
    }
  }
  return cfg;
}

mf::core::BookEvent make_event(mf::core::Venue venue, std::uint64_t seq, std::uint64_t ts) {
  mf::core::BookEvent ev{};
  ev.venue = venue;
  ev.type = mf::core::EventType::Add;
  ev.sequence = seq;
  ev.exchange_ts_ns = ts;
  ev.ingest_ts_ns = ts + 10U;
  ev.side = mf::core::Side::Buy;
  ev.price = static_cast<std::uint32_t>(10000U + (seq & 127U));
  ev.qty = 50U;
  ev.order_id = seq;
  ev.raw_type = static_cast<std::uint8_t>('A');
  ev.symbol.bytes = {'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '};
  return ev;
}

std::vector<mf::core::BookEvent> make_source(std::size_t n) {
  std::vector<mf::core::BookEvent> out;
  out.reserve(n);
  std::uint64_t seq_n = 1;
  std::uint64_t seq_i = 1;
  std::uint64_t seq_c = 1;
  std::uint64_t ts = 1'000'000;
  for (std::size_t i = 0; i < n; ++i) {
    if ((i % 3U) == 0U) {
      out.push_back(make_event(mf::core::Venue::Nasdaq, seq_n++, ts));
    } else if ((i % 3U) == 1U) {
      out.push_back(make_event(mf::core::Venue::Iex, seq_i++, ts));
    } else {
      out.push_back(make_event(mf::core::Venue::Cboe, seq_c++, ts));
    }
    ts += 100U;
  }
  return out;
}

std::uint32_t run_baseline(const std::vector<mf::core::BookEvent>& src, const Config& cfg) {
  mf::phase2::Pipeline p(cfg.gap_window, cfg.capacity);
  for (const auto& ev : src) {
    p.on_event(ev);
  }
  p.finalize();
  return p.stats().merged_crc;
}

struct RaceOutcome {
  std::uint32_t crc{0};
  mf::phase2::AbArbiterStats arb{};
  mf::phase2::PipelineStats pipe{};
  mf::phase2::DroppedFeedCounts drops{};
};

RaceOutcome run_raced(const std::vector<mf::core::BookEvent>& src, const Config& cfg) {
  RaceOutcome out{};
  std::vector<std::pair<mf::phase2::FeedSide, mf::core::BookEvent>> raced;
  if (cfg.complementary_drops) {
    raced.reserve(src.size());
    for (const auto& ev : src) {
      raced.push_back({((ev.sequence & 1ULL) == 0ULL) ? mf::phase2::FeedSide::A : mf::phase2::FeedSide::B, ev});
    }
  } else {
    raced = mf::phase2::make_dual_feed_race_stream(
        src,
        mf::phase2::DualFeedDropConfig{cfg.drop_a, cfg.drop_b, cfg.seed},
        &out.drops);
  }
  mf::phase2::AbArbiter arb(cfg.gap_window);
  mf::phase2::Pipeline p(cfg.gap_window, cfg.capacity);
  for (const auto& pair : raced) {
    (void)arb.on_event(pair.first, pair.second);
    auto ready = arb.drain_ready();
    for (const auto& ev : ready) {
      p.on_event(ev);
    }
  }
  p.finalize();
  out.crc = p.stats().merged_crc;
  out.arb = arb.stats();
  out.pipe = p.stats();
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  const Config cfg = parse_args(argc, argv);

  std::vector<mf::core::BookEvent> source;
  if (cfg.from_files) {
    if (cfg.nasdaq_path.empty() || cfg.iex_path.empty()) {
      std::cerr << "error: --from-files requires --nasdaq and --iex\n";
      return 2;
    }
    try {
      if (cfg.nasdaq_raw_itch) {
        load_nasdaq_raw_itch(cfg.nasdaq_path, source);
      } else {
        load_nasdaq_framed(cfg.nasdaq_path, source);
      }
      load_iex_pcap(cfg.iex_path, source);
      if (!cfg.cboe_path.empty()) {
        load_cboe_framed(cfg.cboe_path, source);
      }
    } catch (const std::exception& ex) {
      std::cerr << "file load error: " << ex.what() << "\n";
      return 1;
    }
  } else {
    source = make_source(cfg.events);
  }

  const std::uint32_t baseline_crc = run_baseline(source, cfg);
  const RaceOutcome race = run_raced(source, cfg);

  std::cout << "[phase2_ab_evidence]\n";
  std::cout << "mode=" << (cfg.from_files ? "real_files" : "synthetic") << "\n";
  std::cout << "events_in=" << source.size() << "\n";
  std::cout << "gap_window=" << cfg.gap_window << "\n";
  std::cout << "capacity=" << cfg.capacity << "\n";
  std::cout << std::fixed << std::setprecision(6);
  std::cout << "drop_rate_a=" << cfg.drop_a << "\n";
  std::cout << "drop_rate_b=" << cfg.drop_b << "\n";
  std::cout << "drop_seed=" << cfg.seed << "\n";
  std::cout << "complementary_drops=" << (cfg.complementary_drops ? "true" : "false") << "\n";
  std::cout << "dropped_a=" << race.drops.dropped_a << "\n";
  std::cout << "dropped_b=" << race.drops.dropped_b << "\n";
  std::cout << std::hex << std::setfill('0');
  std::cout << "baseline_crc32=0x" << std::setw(8) << baseline_crc << "\n";
  std::cout << "raced_crc32=0x" << std::setw(8) << race.crc << "\n";
  std::cout << std::dec;
  std::cout << "crc_match=" << ((baseline_crc == race.crc) ? "true" : "false") << "\n";
  std::cout << "arb_accepted=" << race.arb.accepted << "\n";
  std::cout << "arb_accepted_a=" << race.arb.accepted_a << "\n";
  std::cout << "arb_accepted_b=" << race.arb.accepted_b << "\n";
  std::cout << "arb_duplicate_or_old=" << race.arb.duplicate_or_old << "\n";
  std::cout << "arb_gap_buffered=" << race.arb.gap_buffered << "\n";
  std::cout << "arb_gap_too_large=" << race.arb.gap_too_large << "\n";
  std::cout << "pipe_accepted=" << race.pipe.accepted << "\n";
  std::cout << "pipe_dropped_gap_too_large=" << race.pipe.dropped_gap_too_large << "\n";
  std::cout << "pipe_recovery_requests=" << race.pipe.recovery_requests << "\n";
  return 0;
}
