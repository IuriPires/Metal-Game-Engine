#include "mge/rhi/swapchain.h"

#include "format_conv.h"
#include "mge/renderer/metal/metal_cpp.h"

#include <utility>

namespace mge::rhi {

namespace mb = metal_backend;

Swapchain::~Swapchain() {
    // CAMetalLayer is owned by the platform Window; nothing to release here.
}

void Swapchain::resize(std::uint32_t width, std::uint32_t height) {
    if (native_layer_ == nullptr) {
        return;
    }
    auto* layer = static_cast<CA::MetalLayer*>(native_layer_);
    layer->setDrawableSize(CGSize{static_cast<CGFloat>(width), static_cast<CGFloat>(height)});
}

void Swapchain::set_color_format(PixelFormat fmt) {
    format_ = fmt;
    if (native_layer_ != nullptr) {
        static_cast<CA::MetalLayer*>(native_layer_)->setPixelFormat(mb::to_mtl(fmt));
    }
}

SwapchainFrame Swapchain::acquire_frame() {
    auto* layer = static_cast<CA::MetalLayer*>(native_layer_);
    CA::MetalDrawable* drawable = layer != nullptr ? layer->nextDrawable() : nullptr;
    if (drawable == nullptr) {
        return SwapchainFrame{nullptr, Texture{nullptr, TextureDesc{}, /*owned=*/false}};
    }
    drawable->retain();  // SwapchainFrame owns the reference

    MTL::Texture* tex = drawable->texture();

    TextureDesc desc;
    desc.width   = static_cast<std::uint32_t>(tex->width());
    desc.height  = static_cast<std::uint32_t>(tex->height());
    desc.format  = format_;
    desc.usage   = TextureUsage::RenderTarget;
    desc.storage = StorageMode::Private;
    desc.label   = "drawable";

    return SwapchainFrame{drawable, Texture{tex, std::move(desc), /*owned=*/false}};
}

SwapchainFrame::~SwapchainFrame() {
    if (drawable_ != nullptr) {
        static_cast<CA::MetalDrawable*>(drawable_)->release();
    }
}

SwapchainFrame::SwapchainFrame(SwapchainFrame&& other) noexcept
    : drawable_(other.drawable_), texture_(std::move(other.texture_)) {
    other.drawable_ = nullptr;
}

}  // namespace mge::rhi
