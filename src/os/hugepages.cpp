#include "mf/os/hugepages.hpp"

#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace mf::os {

HugepageAlloc mmap_hugepages(std::size_t bytes, int huge_size_bits) noexcept {
  HugepageAlloc out{};
  out.bytes = bytes;
#if defined(__linux__)
  int huge_flag = 0;
#ifdef MAP_HUGE_SHIFT
  huge_flag = (huge_size_bits << MAP_HUGE_SHIFT);
#endif
  void* p = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | huge_flag, -1, 0);
  if (p != MAP_FAILED) {
    out.ptr = p;
    out.path = HugepagePath::MapHugeTlb;
    return out;
  }
  p = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED) {
    out.path = HugepagePath::None;
    return out;
  }
  out.ptr = p;
  if (::madvise(p, bytes, MADV_HUGEPAGE) == 0) {
    out.path = HugepagePath::MadviseHugepage;
  } else {
    out.path = HugepagePath::RegularPages;
  }
#else
  (void)bytes;
  (void)huge_size_bits;
  out.path = HugepagePath::None;
#endif
  return out;
}

void munmap_hugepages(const HugepageAlloc& alloc) noexcept {
#if defined(__linux__)
  if (alloc.ptr != nullptr && alloc.bytes > 0) {
    (void)::munmap(alloc.ptr, alloc.bytes);
  }
#else
  (void)alloc;
#endif
}

}  // namespace mf::os
