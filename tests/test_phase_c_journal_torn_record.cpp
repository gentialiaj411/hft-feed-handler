#include <cassert>
#include <cstdio>

#include "mf/journal/journal_reader.hpp"
#include "mf/journal/journal_writer.hpp"

#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#endif

int main() {
#if !defined(__linux__)
  std::printf("SKIP: journal tests are Linux-only\n");
  return 0;
#else
  char path[] = "/tmp/mfjn_torn_XXXXXX";
  const int fd = ::mkstemp(path);
  assert(fd >= 0);
  ::close(fd);

  mf::core::BookEvent ev{};
  ev.venue = mf::core::Venue::Nasdaq;
  ev.type = mf::core::EventType::Add;
  ev.sequence = 1;
  ev.ingest_ts_ns = 42;
  ev.exchange_ts_ns = 41;
  ev.price = 100;
  ev.qty = 10;

  mf::journal::JournalWriter writer(1U << 12U, true);
  assert(writer.open(path));
  writer.append(ev, ev.ingest_ts_ns);
  writer.close();

  const int wfd = ::open(path, O_WRONLY);
  assert(wfd >= 0);
  const off_t truncated = static_cast<off_t>(sizeof(mf::journal::JournalHeader) + sizeof(mf::journal::JournalRecord) + 8);
  assert(::ftruncate(wfd, truncated) == 0);
  ::close(wfd);

  mf::journal::JournalReader reader;
  assert(reader.open(path));
  mf::core::BookEvent out{};
  std::uint64_t ts = 0;
  std::uint64_t seq = 0;
  assert(!reader.next(out, ts, seq));
  assert(reader.stats().crc_failures == 1);
  ::unlink(path);
  return 0;
#endif
}
