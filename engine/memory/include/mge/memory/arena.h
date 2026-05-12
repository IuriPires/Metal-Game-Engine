#pragma once

#include "mge/memory/stats.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace mge::memory {

// Bump allocator over a caller-provided buffer. O(1) allocate, O(1) reset.
// No per-object free; the whole arena is reset together.
//
// Thread-safety: not synchronized. One arena per consumer thread is the
// common pattern (e.g. one per frame index for the FrameAllocator).
//
// Alignment: each `allocate(bytes, align)` advances the head to the next
// `align`-byte boundary before claiming `bytes`. `align` must be a power of two.
class Arena {
public:
    Arena() = default;

    Arena(std::byte* buffer, std::size_t bytes) noexcept
        : base_(buffer), head_(buffer), capacity_(bytes) {
        stats_.bytes_capacity = bytes;
    }

    Arena(const Arena&)            = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&&)                 = delete;
    Arena& operator=(Arena&&)      = delete;

    [[nodiscard]] void* allocate(std::size_t bytes, std::size_t align = alignof(std::max_align_t)) noexcept {
        if (base_ == nullptr || bytes == 0) {
            return nullptr;
        }
        const std::uintptr_t cur = reinterpret_cast<std::uintptr_t>(head_);
        const std::uintptr_t mask    = static_cast<std::uintptr_t>(align - 1);
        const std::uintptr_t aligned = (cur + mask) & ~mask;
        const std::byte*     next    = reinterpret_cast<std::byte*>(aligned + bytes);
        if (next > base_ + capacity_) {
            return nullptr;
        }
        void* result = reinterpret_cast<void*>(aligned);
        head_         = const_cast<std::byte*>(next);
        const std::size_t used = static_cast<std::size_t>(head_ - base_);
        stats_.bytes_in_use = used;
        if (used > stats_.bytes_high_water) {
            stats_.bytes_high_water = used;
        }
        ++stats_.num_alloc_calls;
        ++stats_.num_live_allocs;
        return result;
    }

    template <class T, class... Args>
    [[nodiscard]] T* emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        void* p = allocate(sizeof(T), alignof(T));
        if (p == nullptr) {
            return nullptr;
        }
        return ::new (p) T(std::forward<Args>(args)...);
    }

    void reset() noexcept {
        head_                  = base_;
        stats_.bytes_in_use    = 0;
        stats_.num_live_allocs = 0;
        ++stats_.num_free_calls;
    }

    [[nodiscard]] std::size_t bytes_used() const noexcept {
        return base_ ? static_cast<std::size_t>(head_ - base_) : 0;
    }
    [[nodiscard]] std::size_t bytes_remaining() const noexcept {
        return capacity_ - bytes_used();
    }
    [[nodiscard]] std::size_t  capacity() const noexcept { return capacity_; }
    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

private:
    std::byte*  base_     = nullptr;
    std::byte*  head_     = nullptr;
    std::size_t capacity_ = 0;
    Stats       stats_;
};

// Owning variant - allocates its backing storage with operator new(aligned).
class OwnedArena : public Arena {
public:
    explicit OwnedArena(std::size_t bytes, std::size_t align = 64)
        : Arena(), buffer_(static_cast<std::byte*>(
            ::operator new(bytes, std::align_val_t{align}))),
          align_(align) {
        new (static_cast<Arena*>(this)) Arena(buffer_, bytes);
    }

    OwnedArena(const OwnedArena&)            = delete;
    OwnedArena& operator=(const OwnedArena&) = delete;

    ~OwnedArena() {
        ::operator delete(buffer_, std::align_val_t{align_});
    }

private:
    std::byte*  buffer_ = nullptr;
    std::size_t align_  = 64;
};

}  // namespace mge::memory
