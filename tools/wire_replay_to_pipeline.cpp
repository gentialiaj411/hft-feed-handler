#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "mf/core/time.hpp"
#include "mf/phase2/ab_arbiter.hpp"
#include "mf/phase2/pipeline.hpp"
#include "mf/wire/event_loop.hpp"
#include "mf/wire/feed_session.hpp"

namespace {
std::string arg(int argc, char** argv, const std::string& k, const std::string& d) { for (int i = 1; i + 1 < argc; ++i) if (argv[i] == k) return argv[i + 1]; return d; }
std::uint64_t argu64(int argc, char** argv, const std::string& k, std::uint64_t d) { return std::stoull(arg(argc, argv, k, std::to_string(d))); }

mf::wire::WireProtocol parse_protocol(const std::string& p) {
  if (p == "iex") return mf::wire::WireProtocol::IexDeep;
  if (p == "cboe") return mf::wire::WireProtocol::CboePitch;
  return mf::wire::WireProtocol::NasdaqItch50;
}

struct Sink final : mf::phase2::IMergedEventSink {
  std::uint64_t merged{0};
  std::vector<std::uint64_t> lats{};
  void on_merged_event(const mf::core::BookEvent& ev) noexcept override {
    ++merged;
    const std::uint64_t now = mf::core::monotonic_raw_now_ns();
    lats.push_back((now > ev.ingest_ts_ns) ? (now - ev.ingest_ts_ns) : 0ULL);
  }
};
}  // namespace

int main(int argc, char** argv) {
  const std::string protocol_s = arg(argc, argv, "--protocol", "nasdaq");
  const std::string group = arg(argc, argv, "--group", "239.0.0.42");
  const int port = static_cast<int>(argu64(argc, argv, "--port", 31337));
  const std::string iface = arg(argc, argv, "--iface", "127.0.0.1");
  const std::uint64_t target = argu64(argc, argv, "--total-events", 1000000);
  const auto protocol = parse_protocol(protocol_s);

  mf::phase2::AbArbiter arb(1024);
  mf::phase2::Pipeline pipeline(1024, 1U << 20U);
  Sink sink{};

  std::uint64_t frames = 0;
  auto on_event = [&](mf::phase2::FeedSide side, const mf::core::BookEvent& ev) {
    ++frames;
    (void)arb.on_event(side, ev);
    auto ready = arb.drain_ready();
    for (const auto& r : ready) pipeline.on_event(r);
  };

  mf::wire::McastReceiverConfig cfg_a{group, static_cast<std::uint16_t>(port), iface};
  mf::wire::McastReceiverConfig cfg_b{group, static_cast<std::uint16_t>(port + 1), iface};
  mf::wire::FeedSession a(cfg_a, protocol, mf::phase2::FeedSide::A, on_event);
  mf::wire::FeedSession b(cfg_b, protocol, mf::phase2::FeedSide::B, on_event);
  if (!a.open() || !b.open()) return 2;
  mf::wire::WireEventLoop loop;
  if (!loop.add_session(&a) || !loop.add_session(&b)) return 3;

  const std::uint64_t t0 = mf::core::monotonic_raw_now_ns();
  while (sink.merged < target) {
    loop.run_for(std::chrono::milliseconds(20));
    pipeline.finalize(&sink);
  }
  const std::uint64_t t1 = mf::core::monotonic_raw_now_ns();
  pipeline.finalize(&sink);

  std::sort(sink.lats.begin(), sink.lats.end());
  const auto pct = [&](double p) -> std::uint64_t {
    if (sink.lats.empty()) return 0;
    const std::size_t idx = static_cast<std::size_t>((p * static_cast<double>(sink.lats.size() - 1)));
    return sink.lats[idx];
  };
  const double sec = static_cast<double>(t1 - t0) / 1e9;
  const double tput = (sec > 0.0) ? static_cast<double>(sink.merged) / sec : 0.0;

  const auto& as = a.stats();
  const auto& bs = b.stats();
  const auto& ab = arb.stats();
  const auto& ps = pipeline.stats();
  std::printf("datagrams=%llu bytes=%llu frames=%llu parses=%llu accepted-A=%llu accepted-B=%llu dropped-A=%llu dropped-B=%llu gaps=%llu forced-advances=%llu recovered=%llu final-CRC=%u throughput-msgs-per-sec=%.2f p50=%lluns p99=%lluns p99.9=%lluns\n",
      static_cast<unsigned long long>(as.datagrams_received + bs.datagrams_received),
      static_cast<unsigned long long>(as.bytes_received + bs.bytes_received),
      static_cast<unsigned long long>(as.frames_parsed + bs.frames_parsed),
      static_cast<unsigned long long>((as.sink_callbacks_fired + bs.sink_callbacks_fired) - (as.parse_failures + bs.parse_failures)),
      static_cast<unsigned long long>(ab.accepted_a),
      static_cast<unsigned long long>(ab.accepted_b),
      static_cast<unsigned long long>(as.parse_failures),
      static_cast<unsigned long long>(bs.parse_failures),
      static_cast<unsigned long long>(ab.gap_buffered),
      static_cast<unsigned long long>(ab.forced_advances),
      static_cast<unsigned long long>(ps.recovery_reinjected),
      ps.merged_crc, tput,
      static_cast<unsigned long long>(pct(0.50)),
      static_cast<unsigned long long>(pct(0.99)),
      static_cast<unsigned long long>(pct(0.999)));

  const auto utc = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{}; gmtime_r(&utc, &tm);
  char ts[64]; std::strftime(ts, sizeof(ts), "%Y%m%dT%H%M%SZ", &tm);
  std::filesystem::create_directories("bench/results");
  const std::string out = std::string("bench/results/phase_b_wire_") + ts + ".json";
  std::ofstream os(out);
  os << "{\"datagrams\":" << (as.datagrams_received + bs.datagrams_received)
     << ",\"bytes\":" << (as.bytes_received + bs.bytes_received)
     << ",\"frames\":" << (as.frames_parsed + bs.frames_parsed)
     << ",\"accepted_a\":" << ab.accepted_a
     << ",\"accepted_b\":" << ab.accepted_b
     << ",\"forced_advances\":" << ab.forced_advances
     << ",\"recovered\":" << ps.recovery_reinjected
     << ",\"final_crc\":" << ps.merged_crc
     << ",\"throughput_mps\":" << tput
     << ",\"p50_ns\":" << pct(0.50)
     << ",\"p99_ns\":" << pct(0.99)
     << ",\"p999_ns\":" << pct(0.999)
     << "}\n";
  std::printf("json=%s\n", out.c_str());
  return 0;
}
