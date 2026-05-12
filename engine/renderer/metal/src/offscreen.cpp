#include "mge/renderer/metal/offscreen.h"

#include "mge/renderer/metal/device.h"

#include <cstring>

namespace mge::renderer::metal {

namespace {

constexpr NS::UInteger bytes_per_pixel = 4;  // RGBA8 only for now

}  // namespace

Offscreen::Offscreen(Device& device, unsigned width, unsigned height,
                     MTL::PixelFormat fmt) noexcept
    : device_(device), width_(width), height_(height) {
    MTL::TextureDescriptor* td = MTL::TextureDescriptor::alloc()->init();
    td->setTextureType(MTL::TextureType2D);
    td->setPixelFormat(fmt);
    td->setWidth(width_);
    td->setHeight(height_);
    td->setStorageMode(MTL::StorageModePrivate);
    td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    texture_ = device_.mtl()->newTexture(td);
    td->release();

    const NS::UInteger row_bytes  = static_cast<NS::UInteger>(width_) * bytes_per_pixel;
    const NS::UInteger total_size = row_bytes * height_;
    readback_                     = device_.mtl()->newBuffer(total_size, MTL::ResourceStorageModeShared);
}

Offscreen::~Offscreen() {
    if (readback_ != nullptr) {
        readback_->release();
    }
    if (texture_ != nullptr) {
        texture_->release();
    }
}

std::vector<std::uint8_t> Offscreen::clear_and_read(ClearColor c) {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    MTL::CommandBuffer* cmd = device_.queue()->commandBuffer();

    {
        MTL::RenderPassDescriptor* desc = MTL::RenderPassDescriptor::alloc()->init();
        MTL::RenderPassColorAttachmentDescriptor* color =
            desc->colorAttachments()->object(0);
        color->setTexture(texture_);
        color->setLoadAction(MTL::LoadActionClear);
        color->setStoreAction(MTL::StoreActionStore);
        color->setClearColor(to_mtl(c));

        MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(desc);
        enc->setLabel(NS::String::string("offscreen-clear", NS::UTF8StringEncoding));
        enc->endEncoding();
        desc->release();
    }

    {
        const NS::UInteger row_bytes = static_cast<NS::UInteger>(width_) * bytes_per_pixel;
        MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
        blit->copyFromTexture(texture_, 0, 0,
                              MTL::Origin(0, 0, 0),
                              MTL::Size(width_, height_, 1),
                              readback_, 0,
                              row_bytes,
                              row_bytes * height_);
        blit->endEncoding();
    }

    cmd->commit();
    cmd->waitUntilCompleted();

    const std::size_t       total = static_cast<std::size_t>(width_) * height_ * bytes_per_pixel;
    std::vector<std::uint8_t> out(total);
    std::memcpy(out.data(), readback_->contents(), total);

    pool->release();
    return out;
}

std::array<std::uint8_t, 4>
Offscreen::clear_and_read_pixel(ClearColor c, unsigned x, unsigned y) {
    auto bytes = clear_and_read(c);
    const std::size_t idx =
        (static_cast<std::size_t>(y) * width_ + x) * bytes_per_pixel;
    return {bytes[idx + 0], bytes[idx + 1], bytes[idx + 2], bytes[idx + 3]};
}

}  // namespace mge::renderer::metal
