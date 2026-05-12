#include "mge/renderer/metal/clear_renderer.h"

#include "mge/renderer/metal/device.h"
#include "mge/renderer/metal/swapchain.h"

namespace mge::renderer::metal {

ClearRenderer::ClearRenderer(Device& device, Swapchain& swapchain) noexcept
    : device_(device), swapchain_(swapchain) {}

bool ClearRenderer::draw() {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    CA::MetalDrawable* drawable = swapchain_.next_drawable();
    if (drawable == nullptr) {
        pool->release();
        return false;
    }

    MTL::CommandBuffer* cmd = device_.queue()->commandBuffer();

    MTL::RenderPassDescriptor* desc = MTL::RenderPassDescriptor::alloc()->init();
    MTL::RenderPassColorAttachmentDescriptor* color =
        desc->colorAttachments()->object(0);
    color->setTexture(drawable->texture());
    color->setLoadAction(MTL::LoadActionClear);
    color->setStoreAction(MTL::StoreActionStore);
    color->setClearColor(to_mtl(clear_));

    MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(desc);
    enc->setLabel(NS::String::string("clear-pass", NS::UTF8StringEncoding));
    enc->endEncoding();

    cmd->presentDrawable(drawable);
    cmd->commit();

    desc->release();
    pool->release();
    return true;
}

}  // namespace mge::renderer::metal
