#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace latency_lab {

template <typename T>
class SpscRingBuffer {
public:
    explicit SpscRingBuffer(std::size_t capacity)
        : capacity_(capacity + 1), buffer_(capacity_) {}

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    bool push(const T& item) noexcept {
        const std::size_t head = head_.value.load(std::memory_order_relaxed);
        const std::size_t next_head = increment(head);

        if (next_head == tail_.value.load(std::memory_order_acquire)) {
            return false;
        }

        buffer_[head] = item;
        head_.value.store(next_head, std::memory_order_release);
        return true;
    }

    bool pop(T& item) noexcept {
        const std::size_t tail = tail_.value.load(std::memory_order_relaxed);

        if (tail == head_.value.load(std::memory_order_acquire)) {
            return false;
        }

        item = buffer_[tail];
        tail_.value.store(increment(tail), std::memory_order_release);
        return true;
    }

    bool empty() const noexcept {
        return head_.value.load(std::memory_order_acquire) == tail_.value.load(std::memory_order_acquire);
    }

    std::size_t capacity() const noexcept {
        return capacity_ - 1;
    }

private:
    struct alignas(64) PaddedIndex {
        std::atomic<std::size_t> value{0};
        std::uint8_t padding[64 - sizeof(std::atomic<std::size_t>)]{};
    };

    std::size_t increment(std::size_t value) const noexcept {
        ++value;
        return value == capacity_ ? 0 : value;
    }

    std::size_t capacity_;
    std::vector<T> buffer_;
    PaddedIndex head_;
    PaddedIndex tail_;
};

}  // namespace latency_lab
