#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <random>
#include <thread>
#include <vector>

#include "mf/core/spmc_seqlock_ring.hpp"

namespace {

struct Payload {
  std::uint64_t sequence{0};
  std::uint64_t inverse{~0ULL};
  std::uint64_t salt{0};
  std::array<std::uint64_t, 8> words{};
};

Payload make_payload(std::uint64_t sequence, std::uint64_t salt = 0x9e3779b97f4a7c15ULL) {
  Payload p{};
  p.sequence = sequence;
  p.inverse = ~sequence;
  p.salt = salt ^ (sequence * 0xbf58476d1ce4e5b9ULL);
  for (std::size_t i = 0; i < p.words.size(); ++i) {
    p.words[i] = p.sequence ^ p.inverse ^ p.salt ^ static_cast<std::uint64_t>(i);
  }
  return p;
}

bool valid_payload(const Payload& p) {
  if (p.inverse != ~p.sequence) {
    return false;
  }
  for (std::size_t i = 0; i < p.words.size(); ++i) {
    if (p.words[i] != (p.sequence ^ p.inverse ^ p.salt ^ static_cast<std::uint64_t>(i))) {
      return false;
    }
  }
  return true;
}

void test_single_consumer_parity() {
  auto ring = std::make_unique<mf::core::SPMCSeqlockRing<Payload, 1024>>();
  mf::core::SpmcReaderCursor cursor{};

  for (std::uint64_t i = 0; i < 512; ++i) {
    assert(ring->try_publish(make_payload(i)));
  }

  for (std::uint64_t i = 0; i < 512; ++i) {
    Payload out{};
    const auto result = ring->try_read_next(cursor, out);
    assert(result.status == mf::core::SpmcReadStatus::Success);
    assert(out.sequence == i);
    assert(valid_payload(out));
  }

  Payload out{};
  assert(ring->try_read_next(cursor, out).status == mf::core::SpmcReadStatus::Empty);
}

void test_multi_consumer_fanout() {
  auto ring = std::make_unique<mf::core::SPMCSeqlockRing<Payload, 2048>>();
  for (std::uint64_t i = 0; i < 1024; ++i) {
    assert(ring->try_publish(make_payload(i)));
  }

  for (int reader = 0; reader < 4; ++reader) {
    mf::core::SpmcReaderCursor cursor{};
    for (std::uint64_t i = 0; i < 1024; ++i) {
      Payload out{};
      const auto result = ring->try_read_next(cursor, out);
      assert(result.status == mf::core::SpmcReadStatus::Success);
      assert(out.sequence == i);
      assert(valid_payload(out));
    }
  }
}

void test_overrun_behavior() {
  auto ring = std::make_unique<mf::core::SPMCSeqlockRing<Payload, 4>>();
  mf::core::SpmcReaderCursor cursor{};
  for (std::uint64_t i = 0; i < 10; ++i) {
    assert(ring->try_publish(make_payload(i)));
  }

  Payload out{};
  const auto overrun = ring->try_read_next(cursor, out);
  assert(overrun.status == mf::core::SpmcReadStatus::Overrun);
  assert(cursor.next_sequence == 6);
  assert(cursor.overruns == 6);

  for (std::uint64_t i = 6; i < 10; ++i) {
    const auto result = ring->try_read_next(cursor, out);
    assert(result.status == mf::core::SpmcReadStatus::Success);
    assert(out.sequence == i);
    assert(valid_payload(out));
  }
}

void run_stress_once(std::uint32_t seed) {
  constexpr std::uint64_t kEvents = 4096;
  constexpr int kReaders = 8;
  auto ring = std::make_unique<mf::core::SPMCSeqlockRing<Payload, 8192>>();
  std::atomic<bool> start{false};
  std::atomic<bool> writer_done{false};
  std::atomic<std::uint64_t> invalid_reads{0};
  std::atomic<std::uint64_t> retry_limits{0};
  std::atomic<std::uint64_t> overruns{0};
  std::vector<std::thread> readers;
  readers.reserve(kReaders);

  for (int r = 0; r < kReaders; ++r) {
    readers.emplace_back([&, r]() {
      std::mt19937 rng(seed + static_cast<std::uint32_t>(r));
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      mf::core::SpmcReaderCursor cursor{};
      while (!writer_done.load(std::memory_order_acquire) || cursor.next_sequence < ring->published_sequence()) {
        Payload out{};
        const auto result = ring->try_read_next(cursor, out);
        if (result.status == mf::core::SpmcReadStatus::Success) {
          if (!valid_payload(out)) {
            invalid_reads.fetch_add(1, std::memory_order_relaxed);
          }
          if ((rng() & 15U) == 0U) {
            std::this_thread::yield();
          }
        } else if (result.status == mf::core::SpmcReadStatus::RetryLimit) {
          retry_limits.fetch_add(1, std::memory_order_relaxed);
          std::this_thread::yield();
        } else if (result.status == mf::core::SpmcReadStatus::Overrun) {
          overruns.fetch_add(1, std::memory_order_relaxed);
        } else {
          std::this_thread::yield();
        }
        if (cursor.next_sequence == kEvents) {
          break;
        }
      }
    });
  }

  std::thread writer([&]() {
    start.store(true, std::memory_order_release);
    for (std::uint64_t i = 0; i < kEvents; ++i) {
      assert(ring->try_publish(make_payload(i, seed)));
      if ((i & 63ULL) == 0ULL) {
        std::this_thread::yield();
      }
    }
    writer_done.store(true, std::memory_order_release);
  });

  writer.join();
  for (auto& reader : readers) {
    reader.join();
  }

  assert(invalid_reads.load(std::memory_order_relaxed) == 0);
  assert(overruns.load(std::memory_order_relaxed) == 0);
  (void)retry_limits;
}

void test_multithreaded_stress_repeated() {
  for (std::uint32_t i = 0; i < 100; ++i) {
    run_stress_once(0xC0FFEE00U + i);
  }
}

}  // namespace

int main() {
  test_single_consumer_parity();
  test_multi_consumer_fanout();
  test_overrun_behavior();
  test_multithreaded_stress_repeated();
  return 0;
}
