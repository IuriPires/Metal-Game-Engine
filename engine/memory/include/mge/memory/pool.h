#pragma once

#include "mge/memory/stats.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace mge::memory {

// Fixed-size object pool with an intrusive free-list. O(1) allocate and free.
// No fragmentation across slots. Element size and alignment are fixed at
// construction.
//
// Slot is `max(elem_size, sizeof(void*))` so an unused slot can store a
// next-free pointer. Use for ECS component instances, render handles,
// material entries, etc.
//
// Thread-safety: not synchronized.
class Pool {
public:
    Pool() = default;

    Pool(std::byte* buffer, std::size_t bytes, std::size_t elem_size,
         std::size_t elem_align = alignof(std::max_align_t)) noexcept {
        init(buffer, bytes, elem_size, elem_align);
    }

    Pool(const Pool&)            = delete;
    Pool& operator=(const Pool&) = delete;

    void init(std::byte* buffer, std::size_t bytes, std::size_t elem_size,
              std::size_t elem_align) noexcept {
        const std::size_t slot      = elem_size < sizeof(void*) ? sizeof(void*) : elem_size;
        const std::size_t slot_size = align_up(slot, elem_align);
        slot_size_                  = slot_size;
        elem_align_                 = elem_align;

        const std::uintptr_t raw     = reinterpret_cast<std::uintptr_t>(buffer);
        const std::uintptr_t mask    = static_cast<std::uintptr_t>(elem_align - 1);
        const std::uintptr_t aligned = (raw + mask) & ~mask;
        const std::size_t    offset  = static_cast<std::size_t>(aligned - raw);
        if (offset >= bytes) {
            // No room for even one slot.
            base_                 = nullptr;
            capacity_             = 0;
            free_head_            = nullptr;
            stats_.bytes_capacity = 0;
            return;
        }

        base_                 = reinterpret_cast<std::byte*>(aligned);
        capacity_             = (bytes - offset) / slot_size;
        stats_.bytes_capacity = capacity_ * slot_size;
        free_head_            = nullptr;

        // Thread the free list through the slots.
        for (std::size_t i = 0; i < capacity_; ++i) {
            std::byte* p          = base_ + i * slot_size;
            *reinterpret_cast<std::byte**>(p) = free_head_;
            free_head_            = p;
        }
    }

    [[nodiscard]] void* allocate() noexcept {
        if (free_head_ == nullptr) {
            return nullptr;
        }
        std::byte* slot = free_head_;
        free_head_      = *reinterpret_cast<std::byte**>(slot);
        ++stats_.num_alloc_calls;
        ++stats_.num_live_allocs;
        stats_.bytes_in_use = stats_.num_live_allocs * slot_size_;
        if (stats_.bytes_in_use > stats_.bytes_high_water) {
            stats_.bytes_high_water = stats_.bytes_in_use;
        }
        return slot;
    }

    void free(void* p) noexcept {
        if (p == nullptr) {
            return;
        }
        std::byte* slot                    = static_cast<std::byte*>(p);
        *reinterpret_cast<std::byte**>(slot) = free_head_;
        free_head_                          = slot;
        ++stats_.num_free_calls;
        if (stats_.num_live_allocs > 0) {
            --stats_.num_live_allocs;
        }
        stats_.bytes_in_use = stats_.num_live_allocs * slot_size_;
    }

    template <class T, class... Args>
    [[nodiscard]] T* emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        static_assert(sizeof(T) <= 4096, "Pool::emplace assumes a modest T");
        void* p = allocate();
        if (p == nullptr) {
            return nullptr;
        }
        return ::new (p) T(std::forward<Args>(args)...);
    }

    template <class T>
    void destroy(T* obj) noexcept {
        if (obj == nullptr) {
            return;
        }
        obj->~T();
        free(obj);
    }

    [[nodiscard]] std::size_t  capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t  slot_size() const noexcept { return slot_size_; }
    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

private:
    static constexpr std::size_t align_up(std::size_t v, std::size_t a) noexcept {
        return (v + (a - 1)) & ~(a - 1);
    }

    std::byte*  base_       = nullptr;
    std::byte*  free_head_  = nullptr;
    std::size_t capacity_   = 0;
    std::size_t slot_size_  = 0;
    std::size_t elem_align_ = 0;
    Stats       stats_;
};

// Owning variant. Allocates the backing buffer with aligned new.
class OwnedPool : public Pool {
public:
    OwnedPool(std::size_t elem_size, std::size_t count,
              std::size_t elem_align = alignof(std::max_align_t))
        : buffer_align_(elem_align >= alignof(void*) ? elem_align : alignof(void*)) {
        const std::size_t slot = elem_size < sizeof(void*) ? sizeof(void*) : elem_size;
        const std::size_t slot_size =
            (slot + (buffer_align_ - 1)) & ~(buffer_align_ - 1);
        // operator new(align_val_t) returns an aligned buffer, no padding needed.
        bytes_ = slot_size * count;
        buffer_ = static_cast<std::byte*>(
            ::operator new(bytes_, std::align_val_t{buffer_align_}));
        init(buffer_, bytes_, elem_size, elem_align);
    }

    ~OwnedPool() {
        ::operator delete(buffer_, std::align_val_t{buffer_align_});
    }

    OwnedPool(const OwnedPool&)            = delete;
    OwnedPool& operator=(const OwnedPool&) = delete;

private:
    std::byte*  buffer_       = nullptr;
    std::size_t bytes_        = 0;
    std::size_t buffer_align_ = 0;
};

}  // namespace mge::memory
