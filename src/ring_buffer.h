#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <cassert>

namespace market {

template <typename T, size_t Size>
class SPSCRingBuffer {

    static_assert((Size & (Size - 1)) == 0, "Size must be power of two");

    alignas(64) std::atomic<size_t> head_{0};

    alignas(64) std::atomic<size_t> tail_{0};

    alignas(64) std::array<T, Size> buffer_;

    static constexpr size_t mask_ = Size - 1;

public:

    SPSCRingBuffer() = default;

    SPSCRingBuffer(const SPSCRingBuffer&) = delete;
    SPSCRingBuffer& operator=(const SPSCRingBuffer&) = delete;

    bool try_push(const T& item) {

        const size_t head = head_.load(std::memory_order_relaxed);

        const size_t next = (head + 1) & mask_;

        if (next == tail_.load(std::memory_order_acquire)) {
            return false;
        }

        buffer_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool try_pop(T& item) {

        const size_t tail = tail_.load(std::memory_order_relaxed);

        if (tail == head_.load(std::memory_order_acquire)) {
            return false;
        }

        item = buffer_[tail];
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return true;
    }

    size_t size() const {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        if (head >= tail) {
            return head - tail;
        }
        return Size - (tail - head);
    }
};

}
