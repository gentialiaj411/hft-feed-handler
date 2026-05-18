#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

#include "mf/journal/journal_reader.hpp"
#include "mf/journal/journal_writer.hpp"

#if defined(__linux__)
#include <unistd.h>
#endif

namespace {
mf::core::BookEvent mk(std::uint64_t i) {
  mf::core::BookEvent ev{};
  ev.venue = mf::core::Venue::Nasdaq;
  ev.type = mf::core::EventType::Add;
  ev.sequence = i;
  ev.exchange_ts_ns = 1000 + i;
  ev.ingest_ts_ns = 2000 + i;
  ev.order_id = i;
  ev.qty = 100;
  ev.price = 10000 + static_cast<std::uint32_t>(i);
  ev.side = mf::core::Side::Buy;
  ev.symbol.bytes = {'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '};
  return ev;
}
}

int main() {
#if !defined(__linux__)
  std::printf("SKIP: journal tests are Linux-only\n");
  return 0;
#else
  char path[] = "/tmp/mfjn_roundtrip_XXXXXX";
  const int fd = ::mkstemp(path);
  assert(fd >= 0);
  ::close(fd);

  mf::journal::JournalWriter writer(1U << 16U, true);
  assert(writer.open(path));
  std::vector<mf::core::BookEvent> src;
  for (std::uint64_t i = 1; i <= 1024; ++i) {
    src.push_back(mk(i));
    writer.append(src.back(), src.back().ingest_ts_ns);
  }
  writer.close();

  mf::journal::JournalReader reader;
  assert(reader.open(path));
  for (std::size_t i = 0; i < src.size(); ++i) {
    mf::core::BookEvent out{};
    std::uint64_t ts = 0;
    std::uint64_t seq = 0;
    assert(reader.next(out, ts, seq));
    assert(seq == i + 1);
    assert(ts == src[i].ingest_ts_ns);
    assert(std::memcmp(&out, &src[i], sizeof(out)) == 0);
  }
  mf::core::BookEvent dummy{};
  std::uint64_t ts = 0;
  std::uint64_t seq = 0;
  assert(!reader.next(dummy, ts, seq));
  assert(reader.stats().crc_failures == 0);
  ::unlink(path);
  return 0;
#endif
}
