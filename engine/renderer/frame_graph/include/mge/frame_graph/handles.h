#pragma once

#include "mge/rhi/enums.h"

#include <cstdint>
#include <limits>
#include <string>

namespace mge::frame_graph {

// Strong-typed handle into the frame graph's virtual resource table. The
// integer payload is an index into the FrameGraph's internal vector and is
// only meaningful for one (compile, execute) cycle.
struct TextureHandle {
    std::uint32_t id = std::numeric_limits<std::uint32_t>::max();

    [[nodiscard]] constexpr bool valid() const noexcept {
        return id != std::numeric_limits<std::uint32_t>::max();
    }
    constexpr bool operator==(const TextureHandle&) const noexcept = default;
};

struct TransientTextureDesc {
    std::uint32_t   width      = 0;
    std::uint32_t   height     = 0;
    std::uint32_t   mip_levels = 1;
    rhi::PixelFormat format    = rhi::PixelFormat::RGBA8Unorm;
    rhi::TextureUsage usage    = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderRead;
    rhi::StorageMode storage   = rhi::StorageMode::Private;
};

// How a pass uses a resource. Determines the implicit attachment slot when
// the pass begins a render pass.
enum class ResourceUsage : std::uint8_t {
    None = 0,
    ColorAttachment,
    DepthAttachment,
    ShaderRead,
    ShaderWrite,        // compute pass writing a storage texture
    CopySrc,
    CopyDst,
};

}  // namespace mge::frame_graph
