#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "mf/core/time.hpp"
#include "mf/journal/journal_writer.hpp"
#include "mf/journal/journaling_sink.hpp"
#include "mf/phase2/ab_arbiter.hpp"
#include "mf/phase2/pipeline.hpp"
#include "mf/phase4/backtest_runner.hpp"
#include "mf/wire/event_loop.hpp"
#include "mf/wire/feed_session.hpp"

namespace {
std::string arg(int argc, char** argv, const std::string& key, const std::string& dflt) {
  for (int i = 1; i + 1 < argc; ++i) if (argv[i] == key) return argv[i + 1];
  return dflt;
}
std::uint64_t arg_u64(int argc, char** argv, const std::string& key, std::uint64_t dflt) {
  return static_cast<std::uint64_t>(std::stoull(arg(argc, argv, key, std::to_string(dflt))));
}
void be16(std::uint8_t* p, std::uint16_t v) { p[0] = static_cast<std::uint8_t>(v >> 8U); p[1] = static_cast<std::uint8_t>(v); }
void be64(std::uint8_t* p, std::uint64_t v) { for (int i = 7; i >= 0; --i) p[7 - i] = static_cast<std::uint8_t>((v >> (8U * i)) & 0xFFU); }
std::vector<std::uint8_t> make_pkt(std::uint64_t seq) {
  std::vector<std::uint8_t> d(20 + 2 + 36, 0);
  std::memcpy(d.data(), "SESSION0001", 10);
  be64(d.data() + 10, seq); be16(d.data() + 18, 1); be16(d.data() + 20, 36);
  d[22] = 'A'; d[34] = static_cast<std::uint8_t>(seq & 0xFFU); d[43] = ((seq & 1ULL) == 0ULL) ? 'S' : 'B';
  d[47] = 10; d[48] = 'A'; d[49] = 'A'; d[50] = 'P'; d[51] = 'L'; d[57] = static_cast<std::uint8_t>(10 + (seq % 8ULL));
  return d;
}
struct CollectSink final : mf::phase2::IMergedEventSink {
  std::vector<mf::core::BookEvent> merged{};
  void on_merged_event(const mf::core::BookEvent& ev) noexcept override { merged.push_back(ev); }
};
}  // namespace

int main(int argc, char** argv) {
  const std::string journal_path = arg(argc, argv, "--journal", "bench/results/phase_c_live.journal");
  const std::uint64_t total = arg_u64(argc, argv, "--total-events", 1000000);
  const int port = static_cast<int>(arg_u64(argc, argv, "--port", 31557));
  const char* group = "239.0.0.62";

  mf::journal::JournalWriter writer(1U << 20U, true);
  if (!writer.open(journal_path)) {
    std::printf("failed to open journal path=%s\n", journal_path.c_str());
    return 2;
  }

  mf::phase2::AbArbiter arb(1024);
  mf::phase2::Pipeline pipeline(1024, 1U << 20U);
  CollectSink sink{};
  auto live_cb = [&](mf::phase2::FeedSide side, const mf::core::BookEvent& ev) {
    (void)arb.on_event(side, ev);
    auto ready = arb.drain_ready();
    for (const auto& r : ready) pipeline.on_event(r);
  };
  mf::journal::JournalingIngestSink ingest(&writer, live_cb);

  mf::wire::FeedSession session({group, static_cast<std::uint16_t>(port), "127.0.0.1"},
      mf::wire::WireProtocol::NasdaqItch50, mf::phase2::FeedSide::A, ingest);
  if (!session.open()) {
    std::printf("SKIP: receiver open failed\n");
    return 0;
  }

  int tx = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (tx < 0) return 3;
  in_addr iface{}; iface.s_addr = ::inet_addr("127.0.0.1");
  (void)::setsockopt(tx, IPPROTO_IP, IP_MULTICAST_IF, &iface, sizeof(iface));
  sockaddr_in dst{}; dst.sin_family = AF_INET; dst.sin_port = htons(port); dst.sin_addr.s_addr = ::inet_addr(group);

  const std::uint64_t t0 = mf::core::monotonic_raw_now_ns();
  for (std::uint64_t i = 1; i <= total; ++i) {
    auto p = make_pkt(i);
    (void)::sendto(tx, p.data(), p.size(), 0, reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
    if ((i % 10000ULL) == 0ULL) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  ::close(tx);

  mf::wire::WireEventLoop loop;
  if (!loop.add_session(&session)) return 4;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
  while (sink.merged.size() < total && std::chrono::steady_clock::now() < deadline) {
    loop.run_for(std::chrono::milliseconds(10));
    pipeline.finalize(&sink);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const std::uint64_t t1 = mf::core::monotonic_raw_now_ns();
  session.close();
  pipeline.finalize(&sink);
  writer.close();
  if (sink.merged.size() < total) {
    std::printf("incomplete_capture merged=%zu target=%llu parse_failures=%llu\n",
        sink.merged.size(),
        static_cast<unsigned long long>(total),
        static_cast<unsigned long long>(session.stats().parse_failures));
    return 5;
  }

  mf::phase4::BacktestRunner runner;
  const auto rep = runner.run(sink.merged);
  const double sec = static_cast<double>(t1 - t0) / 1e9;
  const double tput = (sec > 0.0) ? static_cast<double>(total) / sec : 0.0;
  std::printf("journal=%s live_pipeline_crc=0x%08x live_pnl=%.6f live_sharpe=%.6f live_fills=%llu live_max_dd=%.6f throughput_rps=%.2f\n",
      journal_path.c_str(),
      pipeline.stats().merged_crc,
      rep.realized_pnl,
      rep.sharpe,
      static_cast<unsigned long long>(rep.fills),
      rep.max_drawdown,
      tput);
  return 0;
}
