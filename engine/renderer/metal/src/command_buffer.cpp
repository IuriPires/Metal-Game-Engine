#include "mge/rhi/command_buffer.h"
#include "mge/rhi/swapchain.h"
#include "mge/rhi/texture.h"

#include "format_conv.h"
#include "mge/renderer/metal/metal_cpp.h"

#include <utility>

namespace mge::rhi {

namespace mb = metal_backend;

CommandBuffer::~CommandBuffer() {
    if (native_ != nullptr) {
        static_cast<MTL::CommandBuffer*>(native_)->release();
    }
}

CommandBuffer::CommandBuffer(CommandBuffer&& other) noexcept
    : native_(other.native_), committed_(other.committed_) {
    other.native_   = nullptr;
    other.committed_ = false;
}

RenderEncoder CommandBuffer::begin_render_pass(const RenderPassDesc& desc) {
    auto* cmd = static_cast<MTL::CommandBuffer*>(native_);

    MTL::RenderPassDescriptor* pd = MTL::RenderPassDescriptor::alloc()->init();

    for (std::uint32_t i = 0; i < desc.num_color_attachments; ++i) {
        const auto& a   = desc.color_attachments[i];
        MTL::RenderPassColorAttachmentDescriptor* d = pd->colorAttachments()->object(i);
        if (a.texture != nullptr) {
            d->setTexture(static_cast<MTL::Texture*>(a.texture->native()));
        }
        d->setLoadAction(mb::to_mtl(a.load_action));
        d->setStoreAction(mb::to_mtl(a.store_action));
        d->setClearColor(MTL::ClearColor::Make(static_cast<double>(a.clear_color[0]),
                                                static_cast<double>(a.clear_color[1]),
                                                static_cast<double>(a.clear_color[2]),
                                                static_cast<double>(a.clear_color[3])));
    }

    if (desc.has_depth && desc.depth_attachment.texture != nullptr) {
        MTL::RenderPassDepthAttachmentDescriptor* d = pd->depthAttachment();
        d->setTexture(static_cast<MTL::Texture*>(desc.depth_attachment.texture->native()));
        d->setLoadAction(mb::to_mtl(desc.depth_attachment.load_action));
        d->setStoreAction(mb::to_mtl(desc.depth_attachment.store_action));
        d->setClearDepth(static_cast<double>(desc.depth_attachment.clear_depth));
    }

    MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pd);
    pd->release();

    if (!desc.label.empty()) {
        enc->setLabel(mb::ns_str(desc.label));
    }
    // Encoder is autoreleased; we don't retain because its lifetime is bounded
    // by the encoder pair {begin, end} and the parent command buffer.
    return RenderEncoder(enc);
}

void CommandBuffer::present(SwapchainFrame& frame) {
    auto* cmd      = static_cast<MTL::CommandBuffer*>(native_);
    auto* drawable = static_cast<CA::MetalDrawable*>(frame.native_drawable());
    if (drawable != nullptr) {
        cmd->presentDrawable(drawable);
    }
}

void CommandBuffer::commit() {
    if (native_ == nullptr || committed_) {
        return;
    }
    static_cast<MTL::CommandBuffer*>(native_)->commit();
    committed_ = true;
}

void CommandBuffer::wait_until_completed() {
    if (native_ == nullptr) {
        return;
    }
    static_cast<MTL::CommandBuffer*>(native_)->waitUntilCompleted();
}

}  // namespace mge::rhi
