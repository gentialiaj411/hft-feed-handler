#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace mf::core {

enum class SpmcReadStatus : std::uint8_t {
  Success = 0,
  Empty = 1,
  Overrun = 2,
  RetryLimit = 3,
};

struct SpmcReadResult {
  SpmcReadStatus status{SpmcReadStatus::Empty};
  std::uint64_t torn_read_retries{0};
  std::uint64_t overruns{0};
};

struct SpmcReaderCursor {
  std::uint64_t next_sequence{0};
  std::uint64_t observed_published_sequence{0};
  std::uint64_t torn_read_retries{0};
  std::uint64_t overruns{0};
};

template <typename T, std::size_t Size>
class SPMCSeqlockRing {
  static_assert((Size & (Size - 1U)) == 0, "Size must be power of two");

 public:
  SPMCSeqlockRing() = default;
  SPMCSeqlockRing(const SPMCSeqlockRing&) = delete;
  SPMCSeqlockRing& operator=(const SPMCSeqlockRing&) = delete;

  bool try_publish(const T& item) noexcept {
    const std::uint64_t seq = next_sequence_;
    Slot& slot = slots_[seq & mask_];
    slot.version.store(encode_version(seq, true), std::memory_order_release);
    slot.value = item;
    slot.version.store(encode_version(seq, false), std::memory_order_release);
    ++next_sequence_;
    published_sequence_.store(next_sequence_, std::memory_order_release);
    return true;
  }

  [[nodiscard]] SpmcReadResult try_read_next(SpmcReaderCursor& cursor, T& out, std::uint32_t max_retries = 128) const noexcept {
    SpmcReadResult result{};
    std::uint64_t published = cursor.observed_published_sequence;
    if (cursor.next_sequence == published) {
      published = published_sequence_.load(std::memory_order_acquire);
      cursor.observed_published_sequence = published;
      if (cursor.next_sequence == published) {
        result.status = SpmcReadStatus::Empty;
        return result;
      }
    }

    if (published > cursor.next_sequence && published - cursor.next_sequence > Size) {
      result.status = SpmcReadStatus::Overrun;
      result.overruns = (published - cursor.next_sequence) - Size;
      cursor.overruns += result.overruns;
      cursor.next_sequence = published - Size;
      return result;
    }

    const std::uint64_t target = cursor.next_sequence;
    const Slot& slot = slots_[target & mask_];
    for (std::uint32_t attempt = 0; attempt < max_retries; ++attempt) {
      const std::uint64_t before = slot.version.load(std::memory_order_acquire);
      if (is_write_in_progress(before) || sequence_from_version(before) != target) {
        ++result.torn_read_retries;
        continue;
      }

      T candidate = slot.value;
      std::atomic_thread_fence(std::memory_order_acquire);
      const std::uint64_t after = slot.version.load(std::memory_order_acquire);
      if (before == after && !is_write_in_progress(after) && sequence_from_version(after) == target) {
        out = candidate;
        ++cursor.next_sequence;
        cursor.torn_read_retries += result.torn_read_retries;
        result.status = SpmcReadStatus::Success;
        return result;
      }
      ++result.torn_read_retries;
    }

    cursor.torn_read_retries += result.torn_read_retries;
    result.status = SpmcReadStatus::RetryLimit;
    return result;
  }

  [[nodiscard]] std::uint64_t published_sequence() const noexcept {
    return published_sequence_.load(std::memory_order_acquire);
  }

  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Size; }

 private:
  struct alignas(64) Slot {
    alignas(64) std::atomic<std::uint64_t> version{encode_version(kNeverWrittenSequence, false)};
    T value{};
  };

  static constexpr std::uint64_t kNeverWrittenSequence = UINT64_MAX >> 1U;
  static constexpr std::size_t mask_ = Size - 1U;

  static constexpr std::uint64_t encode_version(std::uint64_t sequence, bool write_in_progress) noexcept {
    return (sequence << 1U) | (write_in_progress ? 1ULL : 0ULL);
  }

  static constexpr std::uint64_t sequence_from_version(std::uint64_t version) noexcept {
    return version >> 1U;
  }

  static constexpr bool is_write_in_progress(std::uint64_t version) noexcept {
    return (version & 1ULL) != 0ULL;
  }

  alignas(64) std::array<Slot, Size> slots_{};
  alignas(64) std::atomic<std::uint64_t> published_sequence_{0};
  alignas(64) std::uint64_t next_sequence_{0};
};

}  // namespace mf::core
