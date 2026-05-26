#include <cstdio>
#include <cstring>
#include <string>

#include "mf/journal/nbbo_event.hpp"

#if defined(__linux__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
std::string arg(int argc, char** argv, const std::string& key, const std::string& dflt) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] == key) return argv[i + 1];
  }
  return dflt;
}
}  // namespace

int main(int argc, char** argv) {
#if !defined(__linux__)
  std::printf("nbbo_journal_diff is Linux-only\n");
  return 0;
#else
  const std::string lhs = arg(argc, argv, "--lhs", "");
  const std::string rhs = arg(argc, argv, "--rhs", "");
  if (lhs.empty() || rhs.empty()) {
    std::fprintf(stderr, "usage: nbbo_journal_diff --lhs <path> --rhs <path>\n");
    return 2;
  }

  auto open_map = [](const std::string& p, int& fd, std::size_t& n, const std::byte*& b) -> bool {
    fd = ::open(p.c_str(), O_RDONLY);
    if (fd < 0) return false;
    struct stat st {};
    if (::fstat(fd, &st) != 0 || st.st_size <= 0) return false;
    n = static_cast<std::size_t>(st.st_size);
    void* m = ::mmap(nullptr, n, PROT_READ, MAP_PRIVATE, fd, 0);
    if (m == MAP_FAILED) return false;
    b = reinterpret_cast<const std::byte*>(m);
    return true;
  };

  int lfd = -1;
  int rfd = -1;
  std::size_t ln = 0;
  std::size_t rn = 0;
  const std::byte* lb = nullptr;
  const std::byte* rb = nullptr;
  if (!open_map(lhs, lfd, ln, lb) || !open_map(rhs, rfd, rn, rb)) {
    std::fprintf(stderr, "failed to open/map journal(s)\n");
    return 1;
  }

  constexpr std::size_t kHdr = sizeof(mf::journal::NbboJournalHeader);
  constexpr std::size_t kRec = sizeof(mf::journal::NbboJournalRecord) + sizeof(mf::journal::NbboEvent);
  if (ln < kHdr || rn < kHdr) {
    std::fprintf(stderr, "invalid journal header size\n");
    return 1;
  }
  if (std::memcmp(lb, rb, kHdr) != 0) {
    std::fprintf(stderr, "header mismatch\n");
    return 1;
  }
  const std::size_t lrecs = (ln - kHdr) / kRec;
  const std::size_t rrecs = (rn - kHdr) / kRec;
  if (lrecs != rrecs) {
    std::fprintf(stderr, "record_count_mismatch lhs=%zu rhs=%zu\n", lrecs, rrecs);
    return 1;
  }
  for (std::size_t i = 0; i < lrecs; ++i) {
    const auto* lrec = lb + kHdr + (i * kRec);
    const auto* rrec = rb + kHdr + (i * kRec);
    if (std::memcmp(lrec, rrec, kRec) != 0) {
      std::fprintf(stderr, "diverged_at_record=%zu\n", i);
      return 1;
    }
  }
  std::printf("identical records=%zu payload_crc_match=true\n", lrecs);
  (void)::munmap(const_cast<std::byte*>(lb), ln);
  (void)::munmap(const_cast<std::byte*>(rb), rn);
  (void)::close(lfd);
  (void)::close(rfd);
  return 0;
#endif
}
