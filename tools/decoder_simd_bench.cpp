#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "mf/bench/run_metadata.hpp"
#include "mf/bench/tsc_clock.hpp"
#include "mf/proto/nasdaq/itch50_parser.hpp"

namespace {

struct Config {
  std::uint64_t messages{1'000'000};
  std::uint64_t warmup{50'000};
  std::string out_dir{"bench/results"};
};

Config parse_args(int argc, char** argv) {
  Config cfg{};
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--messages" && i + 1 < argc) cfg.messages = std::stoull(argv[++i]);
    else if (arg == "--warmup" && i + 1 < argc) cfg.warmup = std::stoull(argv[++i]);
    else if (arg == "--out-dir" && i + 1 < argc) cfg.out_dir = argv[++i];
  }
  return cfg;
}

std::string stamp() {
  const auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &tt);
#else
  gmtime_r(&tt, &tm);
#endif
  char out[64]{};
  std::strftime(out, sizeof(out), "%Y%m%dT%H%M%SZ", &tm);
  return out;
}

void put_be(std::vector<std::byte>& p, std::size_t off, std::uint64_t value, int width) {
  for (int i = 0; i < width; ++i) {
    p[off + static_cast<std::size_t>(i)] =
        std::byte{static_cast<unsigned char>((value >> ((width - 1 - i) * 8)) & 0xffU)};
  }
}

std::vector<std::byte> make_payload(std::uint64_t i) {
  const char type = "AEXD"[i & 3U];
  if (type == 'A') {
    std::vector<std::byte> p(36);
    p[0] = std::byte{'A'};
    put_be(p, 5, 1'000'000 + i, 6);
    put_be(p, 11, 10'000 + i, 8);
    p[19] = (i & 4U) ? std::byte{'S'} : std::byte{'B'};
    put_be(p, 20, 100 + (i & 15U), 4);
    const char symbol[8] = {'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '};
    std::memcpy(p.data() + 24, symbol, sizeof(symbol));
    put_be(p, 32, 99'000 + (i & 31U), 4);
    return p;
  }
  if (type == 'E') {
    std::vector<std::byte> p(31);
    p[0] = std::byte{'E'};
    put_be(p, 5, 2'000'000 + i, 6);
    put_be(p, 11, 10'000 + i, 8);
    put_be(p, 19, 20 + (i & 7U), 4);
    put_be(p, 23, 70'000 + i, 8);
    return p;
  }
  if (type == 'X') {
    std::vector<std::byte> p(23);
    p[0] = std::byte{'X'};
    put_be(p, 5, 3'000'000 + i, 6);
    put_be(p, 11, 10'000 + i, 8);
    put_be(p, 19, 10 + (i & 7U), 4);
    return p;
  }
  std::vector<std::byte> p(19);
  p[0] = std::byte{'D'};
  put_be(p, 5, 4'000'000 + i, 6);
  put_be(p, 11, 10'000 + i, 8);
  return p;
}

template <typename Fn>
double run_decode(const std::vector<std::vector<std::byte>>& payloads, double ticks_per_ns, Fn fn, std::uint64_t& parsed) {
  mf::proto::nasdaq::Itch50Parser parser;
  mf::proto::nasdaq::ParseStats stats{};
  const auto t0 = mf::bench::tsc_now();
  for (std::size_t i = 0; i < payloads.size(); ++i) {
    const auto ev = fn(parser, payloads[i], i + 1, i + 99, stats);
    if (ev.has_value()) ++parsed;
  }
  const auto t1 = mf::bench::tsc_now();
  const auto ns = mf::bench::ticks_to_ns(t1 - t0, ticks_per_ns);
  return ns > 0 ? static_cast<double>(payloads.size()) * 1'000'000'000.0 / static_cast<double>(ns) : 0.0;
}

}  // namespace

int main(int argc, char** argv) {
  const auto cfg = parse_args(argc, argv);
  std::vector<std::vector<std::byte>> warmup;
  warmup.reserve(static_cast<std::size_t>(cfg.warmup));
  for (std::uint64_t i = 0; i < cfg.warmup; ++i) warmup.push_back(make_payload(i));
  std::vector<std::vector<std::byte>> payloads;
  payloads.reserve(static_cast<std::size_t>(cfg.messages));
  for (std::uint64_t i = 0; i < cfg.messages; ++i) payloads.push_back(make_payload(i));

  const double ticks_per_ns = mf::bench::calibrate_ticks_per_ns();
  std::uint64_t ignored = 0;
  (void)run_decode(warmup, ticks_per_ns, [](auto& parser, const auto& p, auto seq, auto ts, auto& stats) {
    return parser.parse_message(p, seq, ts, stats);
  }, ignored);
  (void)run_decode(warmup, ticks_per_ns, [](auto& parser, const auto& p, auto seq, auto ts, auto& stats) {
    return parser.parse_hot_message_simd(p, seq, ts, stats);
  }, ignored);

  std::uint64_t scalar_parsed = 0;
  std::uint64_t simd_parsed = 0;
  const double scalar_mps = run_decode(payloads, ticks_per_ns, [](auto& parser, const auto& p, auto seq, auto ts, auto& stats) {
    return parser.parse_message(p, seq, ts, stats);
  }, scalar_parsed);
  const double simd_mps = run_decode(payloads, ticks_per_ns, [](auto& parser, const auto& p, auto seq, auto ts, auto& stats) {
    return parser.parse_hot_message_simd(p, seq, ts, stats);
  }, simd_parsed);
  const double speedup = scalar_mps > 0.0 ? simd_mps / scalar_mps : 0.0;

  std::filesystem::create_directories(cfg.out_dir);
  const auto path = cfg.out_dir + "/decoder_simd_" + stamp() + ".md";
  auto meta = mf::bench::capture_run_metadata(argc, argv);
  std::ofstream os(path);
  os << "# Decoder SIMD Benchmark\n\n";
  os << "- messages: " << cfg.messages << "\n";
  os << "- warmup: " << cfg.warmup << "\n";
  os << "- simd_hot_path_available: " << (mf::proto::nasdaq::Itch50Parser::simd_hot_path_available() ? "true" : "false") << "\n";
  os << "- ticks_per_ns: " << ticks_per_ns << "\n\n";
  os << "| decoder | parsed | messages_per_sec |\n";
  os << "|---|---:|---:|\n";
  os << "| scalar_reference | " << scalar_parsed << " | " << scalar_mps << " |\n";
  os << "| hot_simd_path | " << simd_parsed << " | " << simd_mps << " |\n";
  os << "| speedup | | " << speedup << "x |\n\n";
  os << "Metadata: " << mf::bench::run_metadata_to_json(meta) << "\n";
  std::cout << "decoder_report=" << path << "\n";
  std::cout << "speedup=" << speedup << "\n";
  return 0;
}
