#include <cassert>
#include <cstdio>

#include "mf/os/hugepages.hpp"

int main() {
#if !defined(__linux__)
  std::printf("SKIP: Linux-only\n");
  return 0;
#else
  auto alloc = mf::os::mmap_hugepages(2U << 20U, 21);
  assert(alloc.ptr != nullptr || alloc.path == mf::os::HugepagePath::None);
  if (alloc.ptr != nullptr) {
    if (alloc.path != mf::os::HugepagePath::MapHugeTlb) {
      assert(alloc.path == mf::os::HugepagePath::MadviseHugepage || alloc.path == mf::os::HugepagePath::RegularPages);
    }
    mf::os::munmap_hugepages(alloc);
  }
  return 0;
#endif
}
