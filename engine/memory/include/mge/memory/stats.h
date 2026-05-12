#pragma once

#include <atomic>
#include <cstddef>

namespace mge::memory {

// Shared shape across allocators. Surfaces in the overlay (M11) and Tracy.
struct Stats {
    std::size_t bytes_in_use     = 0;
    std::size_t bytes_high_water = 0;
    std::size_t bytes_capacity   = 0;
    std::size_t num_live_allocs  = 0;
    std::size_t num_alloc_calls  = 0;
    std::size_t num_free_calls   = 0;
};

}  // namespace mge::memory
