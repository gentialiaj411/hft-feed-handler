#pragma once

#include <cstddef>

namespace mf::os {

enum class HugepagePath {
  None = 0,
  MapHugeTlb = 1,
  MadviseHugepage = 2,
  RegularPages = 3,
};

struct HugepageAlloc {
  void* ptr{nullptr};
  std::size_t bytes{0};
  HugepagePath path{HugepagePath::None};
};

// Requires vm.nr_hugepages setup for MAP_HUGETLB. If it fails, this falls back
// to regular anonymous mmap + madvise(MADV_HUGEPAGE) and reports the path taken.
HugepageAlloc mmap_hugepages(std::size_t bytes, int huge_size_bits = 21) noexcept;
void munmap_hugepages(const HugepageAlloc& alloc) noexcept;

}  // namespace mf::os
