#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "mf/journal/journal_reader.hpp"
#include "mf/journal/journaling_sink.hpp"
#include "mf/journal/journal_writer.hpp"
#include "mf/phase2/ab_arbiter.hpp"
#include "mf/phase2/pipeline.hpp"
#include "mf/phase4/backtest_runner.hpp"
#include "mf/wire/event_loop.hpp"
#include "mf/wire/feed_session.hpp"

#if defined(__linux__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

void be16(std::uint8_t* p, std::uint16_t v) { p[0] = static_cast<std::uint8_t>(v >> 8U); p[1] = static_cast<std::uint8_t>(v); }
void be64(std::uint8_t* p, std::uint64_t v) { for (int i = 7; i >= 0; --i) p[7 - i] = static_cast<std::uint8_t>((v >> (8U * i)) & 0xFFU); }

std::vector<std::uint8_t> make_pkt(std::uint64_t seq) {
  std::vector<std::uint8_t> d(20 + 2 + 36, 0);
  std::memcpy(d.data(), "SESSION0001", 10);
  be64(d.data() + 10, seq);
  be16(d.data() + 18, 1);
  be16(d.data() + 20, 36);
  d[22] = 'A';
  d[34] = static_cast<std::uint8_t>(seq & 0xFFU);
  d[43] = ((seq & 1ULL) == 0ULL) ? 'S' : 'B';
  d[47] = 10;
  d[48] = 'A'; d[49] = 'A'; d[50] = 'P'; d[51] = 'L';
  d[57] = static_cast<std::uint8_t>(10 + (seq % 8ULL));
  return d;
}

struct CollectSink final : mf::phase2::IMergedEventSink {
  std::vector<mf::core::BookEvent> merged{};
  void on_merged_event(const mf::core::BookEvent& ev) noexcept override { merged.push_back(ev); }
};

}  // namespace

int main() {
#if !defined(__linux__)
  std::printf("SKIP: replay determinism test is Linux-only\n");
  return 0;
#else
  const char* group = "239.0.0.52";
  const int port = 31457;
  constexpr int kN = 20000;
  char path[] = "/tmp/mfjn_replay_XXXXXX";
  const int tmp = ::mkstemp(path);
  assert(tmp >= 0);
  ::close(tmp);

  mf::phase2::AbArbiter arb(1024);
  mf::phase2::Pipeline live_pipeline(1024, 1U << 16U);
  CollectSink live_sink{};
  mf::journal::JournalWriter writer(1U << 20U, true);
  assert(writer.open(path));

  auto base_sink = [&](mf::phase2::FeedSide side, const mf::core::BookEvent& ev) {
    (void)arb.on_event(side, ev);
    auto ready = arb.drain_ready();
    for (const auto& r : ready) {
      live_pipeline.on_event(r);
      live_pipeline.finalize(&live_sink);
    }
  };
  mf::journal::JournalingIngestSink sink(&writer, base_sink);

  mf::wire::FeedSession session(
      {group, static_cast<std::uint16_t>(port), "127.0.0.1"},
      mf::wire::WireProtocol::NasdaqItch50,
      mf::phase2::FeedSide::A,
      sink);
  if (!session.open()) {
    std::printf("SKIP: multicast receiver open failed\n");
    ::unlink(path);
    return 0;
  }

  int tx = ::socket(AF_INET, SOCK_DGRAM, 0);
  assert(tx >= 0);
  in_addr iface{};
  iface.s_addr = ::inet_addr("127.0.0.1");
  assert(::setsockopt(tx, IPPROTO_IP, IP_MULTICAST_IF, &iface, sizeof(iface)) == 0);
  sockaddr_in dst{};
  dst.sin_family = AF_INET;
  dst.sin_port = htons(port);
  dst.sin_addr.s_addr = ::inet_addr(group);
  for (int i = 0; i < kN; ++i) {
    auto p = make_pkt(static_cast<std::uint64_t>(i + 1));
    (void)::sendto(tx, p.data(), p.size(), 0, reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
  }
  ::close(tx);

  mf::wire::WireEventLoop loop;
  assert(loop.add_session(&session));
  for (int i = 0; i < 400 && live_sink.merged.size() < static_cast<std::size_t>(kN); ++i) {
    loop.run_for(std::chrono::milliseconds(5));
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  session.close();
  live_pipeline.finalize(&live_sink);
  writer.close();

  assert(live_sink.merged.size() == static_cast<std::size_t>(kN));
  const auto live_crc = live_pipeline.stats().merged_crc;
  mf::phase4::BacktestRunner live_runner;
  const auto live_rep = live_runner.run(live_sink.merged);

  mf::journal::JournalReader reader;
  assert(reader.open(path));
  mf::phase2::Pipeline replay_pipeline(1024, 1U << 16U);
  CollectSink replay_sink{};
  mf::core::BookEvent ev{};
  std::uint64_t ts = 0;
  std::uint64_t seq = 0;
  while (reader.next(ev, ts, seq)) {
    (void)seq;
    ev.ingest_ts_ns = ts;
    replay_pipeline.on_event(ev);
  }
  replay_pipeline.finalize(&replay_sink);
  assert(reader.stats().crc_failures == 0);
  assert(replay_sink.merged.size() == live_sink.merged.size());

  const auto replay_crc = replay_pipeline.stats().merged_crc;
  mf::phase4::BacktestRunner replay_runner;
  const auto replay_rep = replay_runner.run(replay_sink.merged);

  assert(live_crc == replay_crc);
  assert(live_rep.realized_pnl == replay_rep.realized_pnl);
  assert(live_rep.sharpe == replay_rep.sharpe);
  assert(live_rep.fills == replay_rep.fills);
  assert(live_rep.max_drawdown == replay_rep.max_drawdown);

  ::unlink(path);
  return 0;
#endif
}
