#include <cassert>
#include <cstdio>
#include <cstring>

#include "mf/journal/journal_format.hpp"
#include "mf/journal/journal_reader.hpp"

#if defined(__linux__)
#include <unistd.h>
#endif

int main() {
#if !defined(__linux__)
  std::printf("SKIP: journal tests are Linux-only\n");
  return 0;
#else
  char path[] = "/tmp/mfjn_badver_XXXXXX";
  const int fd = ::mkstemp(path);
  assert(fd >= 0);

  mf::journal::JournalHeader hdr{};
  hdr.magic = {'B', 'A', 'D', '!'};
  hdr.version = 999;
  assert(::write(fd, &hdr, sizeof(hdr)) == static_cast<ssize_t>(sizeof(hdr)));
  ::close(fd);

  mf::journal::JournalReader reader;
  assert(!reader.open(path));
  ::unlink(path);
  return 0;
#endif
}
