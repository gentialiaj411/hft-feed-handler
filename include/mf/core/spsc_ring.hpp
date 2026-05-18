#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace mf::core {

template <typename T, std::size_t Size>
class SPSCRingBuffer {
  static_assert((Size & (Size - 1)) == 0, "Size must be power of two");

 public:
  SPSCRingBuffer() = default;
  explicit SPSCRingBuffer(T* external_storage, std::size_t capacity = Size) noexcept
      : buffer_(external_storage), external_storage_(true) {
    if (external_storage == nullptr || capacity != Size) {
      buffer_ = owned_buffer_.data();
      external_storage_ = false;
    }
  }
  SPSCRingBuffer(const SPSCRingBuffer&) = delete;
  SPSCRingBuffer& operator=(const SPSCRingBuffer&) = delete;

  bool try_push(const T& item) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t next = (head + 1U) & mask_;
    if (next == tail_.load(std::memory_order_acquire)) {
      return false;
    }
    buffer_ptr()[head] = item;
    head_.store(next, std::memory_order_release);
    return true;
  }

  bool try_pop(T& item) noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) {
      return false;
    }
    item = buffer_ptr()[tail];
    tail_.store((tail + 1U) & mask_, std::memory_order_release);
    return true;
  }

  [[nodiscard]] std::size_t size() const noexcept {
    const std::size_t head = head_.load(std::memory_order_acquire);
    const std::size_t tail = tail_.load(std::memory_order_acquire);
    if (head >= tail) {
      return head - tail;
    }
    return Size - (tail - head);
  }

 private:
  T* buffer_ptr() noexcept { return external_storage_ ? buffer_ : owned_buffer_.data(); }
  const T* buffer_ptr() const noexcept { return external_storage_ ? buffer_ : owned_buffer_.data(); }

  static constexpr std::size_t mask_ = Size - 1U;
  alignas(64) std::atomic<std::size_t> head_{0};
  alignas(64) std::atomic<std::size_t> tail_{0};
  alignas(64) std::array<T, Size> owned_buffer_{};
  T* buffer_{nullptr};
  bool external_storage_{false};
};

}  // namespace mf::core
