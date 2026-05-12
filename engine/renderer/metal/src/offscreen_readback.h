#pragma once

#include "mge/rhi/device.h"
#include "mge/rhi/texture.h"

#include <cstdint>
#include <vector>

namespace mge::rhi::metal_backend {

// Synchronous blit of a Private texture to a CPU-visible buffer, then copy
// to a vector. RGBA8 only for now; expand at M9 when we need other formats.
// Test-only utility (heavy: creates a transient buffer and stalls the GPU).
[[nodiscard]] std::vector<std::uint8_t> read_rgba8(Device& device, Texture& tex);

}  // namespace mge::rhi::metal_backend
