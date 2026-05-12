#include "mge/renderer/metal/swapchain.h"

#include "mge/renderer/metal/device.h"

namespace mge::renderer::metal {

Swapchain::Swapchain(Device& device, CA::MetalLayer* layer) noexcept
    : device_(device), layer_(layer) {
    if (layer_ != nullptr) {
        layer_->setDevice(device_.mtl());
        if (layer_->pixelFormat() == MTL::PixelFormatInvalid) {
            layer_->setPixelFormat(MTL::PixelFormatBGRA8Unorm_sRGB);
        }
        layer_->setFramebufferOnly(true);
    }
}

void Swapchain::resize(unsigned width, unsigned height) noexcept {
    if (layer_ == nullptr) {
        return;
    }
    CGSize sz{static_cast<CGFloat>(width), static_cast<CGFloat>(height)};
    layer_->setDrawableSize(sz);
}

void Swapchain::set_pixel_format(MTL::PixelFormat fmt) noexcept {
    if (layer_ != nullptr) {
        layer_->setPixelFormat(fmt);
    }
}

MTL::PixelFormat Swapchain::pixel_format() const noexcept {
    return layer_ != nullptr ? layer_->pixelFormat() : MTL::PixelFormatInvalid;
}

CA::MetalDrawable* Swapchain::next_drawable() {
    if (layer_ == nullptr) {
        return nullptr;
    }
    return layer_->nextDrawable();
}

}  // namespace mge::renderer::metal
