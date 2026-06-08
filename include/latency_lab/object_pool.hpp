#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace latency_lab {

template <typename T>
class ObjectPool {
public:
    explicit ObjectPool(std::size_t capacity)
        : storage_(capacity), in_use_(capacity, false) {
        free_list_.reserve(capacity);
        for (std::size_t index = capacity; index > 0; --index) {
            free_list_.push_back(index - 1);
        }
    }

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    ~ObjectPool() {
        for (std::size_t index = 0; index < in_use_.size(); ++index) {
            if (in_use_[index]) {
                ptr(index)->~T();
            }
        }
    }

    template <typename... Args>
    T* allocate(Args&&... args) {
        if (free_list_.empty()) {
            return nullptr;
        }

        const std::size_t index = free_list_.back();
        free_list_.pop_back();
        in_use_[index] = true;
        ++allocations_;

        return new (&storage_[index]) T(std::forward<Args>(args)...);
    }

    void deallocate(T* object) noexcept {
        if (object == nullptr) {
            return;
        }

        const auto* base = reinterpret_cast<const std::byte*>(storage_.data());
        const auto* current = reinterpret_cast<const std::byte*>(object);
        const std::size_t index = static_cast<std::size_t>(current - base) / sizeof(Storage);

        object->~T();
        in_use_[index] = false;
        free_list_.push_back(index);
        ++deallocations_;
    }

    std::size_t capacity() const noexcept {
        return storage_.size();
    }

    std::size_t available() const noexcept {
        return free_list_.size();
    }

    std::size_t in_use() const noexcept {
        return capacity() - available();
    }

    std::uint64_t allocations() const noexcept {
        return allocations_;
    }

    std::uint64_t deallocations() const noexcept {
        return deallocations_;
    }

private:
    using Storage = std::aligned_storage_t<sizeof(T), alignof(T)>;

    T* ptr(std::size_t index) noexcept {
        return std::launder(reinterpret_cast<T*>(&storage_[index]));
    }

    std::vector<Storage> storage_;
    std::vector<std::size_t> free_list_;
    std::vector<bool> in_use_;
    std::uint64_t allocations_ = 0;
    std::uint64_t deallocations_ = 0;
};

}
