#include "offscreen_readback.h"

#include "mge/renderer/metal/metal_cpp.h"

#include <cstring>

namespace mge::rhi::metal_backend {

std::vector<std::uint8_t> read_rgba8(Device& device, Texture& tex) {
    auto* dev      = static_cast<MTL::Device*>(device.native());
    auto* mtl_tex  = static_cast<MTL::Texture*>(tex.native());
    if (dev == nullptr || mtl_tex == nullptr) {
        return {};
    }

    const NS::UInteger w   = mtl_tex->width();
    const NS::UInteger h   = mtl_tex->height();
    const NS::UInteger bpp = 4;  // RGBA8
    const NS::UInteger row = w * bpp;
    const NS::UInteger total = row * h;

    MTL::Buffer* buf = dev->newBuffer(total, MTL::ResourceStorageModeShared);
    if (buf == nullptr) {
        return {};
    }

    MTL::CommandQueue*   q   = dev->newCommandQueue();
    MTL::CommandBuffer*  cmd = q->commandBuffer();
    MTL::BlitCommandEncoder* enc = cmd->blitCommandEncoder();
    enc->copyFromTexture(mtl_tex, 0, 0, MTL::Origin(0, 0, 0),
                         MTL::Size(w, h, 1),
                         buf, 0, row, total);
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();

    std::vector<std::uint8_t> out(total);
    std::memcpy(out.data(), buf->contents(), total);

    buf->release();
    q->release();
    return out;
}

}  // namespace mge::rhi::metal_backend
