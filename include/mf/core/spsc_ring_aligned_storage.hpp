#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>

#include "mf/core/spsc_ring.hpp"
#include "mf/os/hugepages.hpp"
#include "mf/os/numa.hpp"

namespace mf::core {

enum class SpscBackingKind {
  Heap = 0,
  Numa = 1,
  HugeTlb = 2,
  MadviseHuge = 3,
};

struct SpscBackingInfo {
  SpscBackingKind kind{SpscBackingKind::Heap};
  std::size_t bytes{0};
  int numa_node{-1};
};

template <typename T, std::size_t Size>
struct SpscRingDeleter {
  void* storage{nullptr};
  std::size_t bytes{0};
  bool use_numa{false};
  bool use_huge{false};

  void operator()(SPSCRingBuffer<T, Size>* p) const noexcept {
    if (p == nullptr) return;
    p->~SPSCRingBuffer<T, Size>();
    if (storage != nullptr) {
      auto* typed = reinterpret_cast<T*>(storage);
      for (std::size_t i = 0; i < Size; ++i) {
        typed[i].~T();
      }
#if defined(__linux__)
      if (use_huge) {
        mf::os::munmap_hugepages({storage, bytes, mf::os::HugepagePath::None});
      } else if (use_numa) {
        mf::os::numa_free(storage, bytes);
      } else
#endif
      {
        ::operator delete(storage, std::align_val_t{alignof(T)});
      }
    }
    ::operator delete(p, std::align_val_t{alignof(SPSCRingBuffer<T, Size>)});
  }
};

template <typename T, std::size_t Size>
using SpscRingPtr = std::unique_ptr<SPSCRingBuffer<T, Size>, SpscRingDeleter<T, Size>>;

template <typename T, std::size_t Size>
SpscRingPtr<T, Size> make_spsc_ring_on_node(int numa_node, bool try_hugepages, SpscBackingInfo& info_out) {
  constexpr std::size_t kBytes = sizeof(T) * Size;
  info_out = {};
  info_out.bytes = kBytes;
  info_out.numa_node = numa_node;

  void* storage = nullptr;
  SpscRingDeleter<T, Size> deleter{};

  if (try_hugepages) {
#if defined(__linux__)
    auto hp = mf::os::mmap_hugepages(kBytes);
    if (hp.ptr != nullptr) {
      storage = hp.ptr;
      deleter.storage = storage;
      deleter.bytes = kBytes;
      deleter.use_huge = true;
      info_out.kind = (hp.path == mf::os::HugepagePath::MapHugeTlb) ? SpscBackingKind::HugeTlb : SpscBackingKind::MadviseHuge;
    }
#else
    (void)try_hugepages;
#endif
  }
  if (storage == nullptr) {
#if defined(__linux__)
    storage = mf::os::numa_alloc_on_node(kBytes, numa_node);
    if (storage != nullptr) {
      deleter.storage = storage;
      deleter.bytes = kBytes;
      deleter.use_numa = true;
      info_out.kind = mf::os::numa_available() ? SpscBackingKind::Numa : SpscBackingKind::Heap;
    }
#else
    (void)numa_node;
#endif
  }
  if (storage == nullptr) {
    storage = ::operator new(kBytes, std::align_val_t{alignof(T)});
    deleter.storage = storage;
    deleter.bytes = kBytes;
    info_out.kind = SpscBackingKind::Heap;
  }
  auto* typed = reinterpret_cast<T*>(storage);
  for (std::size_t i = 0; i < Size; ++i) {
    new (typed + i) T{};
  }

  auto* ring_mem = ::operator new(sizeof(SPSCRingBuffer<T, Size>), std::align_val_t{alignof(SPSCRingBuffer<T, Size>)});
  auto* ring = new (ring_mem) SPSCRingBuffer<T, Size>(typed, Size);
  return SpscRingPtr<T, Size>(ring, deleter);
}

}  // namespace mf::core
