#pragma once

#include "mge/renderer/metal/metal_cpp.h"

namespace mge::renderer::metal {

class Device;

// Owns a non-owning reference to a CA::MetalLayer (which lives on the
// window). Provides acquire-drawable + present, abstracts away the
// boilerplate. M3+ this folds into the RHI swapchain abstraction.
class Swapchain {
public:
    Swapchain(Device& device, CA::MetalLayer* layer) noexcept;

    void resize(unsigned width, unsigned height) noexcept;
    void set_pixel_format(MTL::PixelFormat fmt) noexcept;

    [[nodiscard]] CA::MetalLayer*   layer() noexcept { return layer_; }
    [[nodiscard]] MTL::PixelFormat  pixel_format() const noexcept;

    // Returns next drawable; caller does not retain. Releases via auto-pool.
    [[nodiscard]] CA::MetalDrawable* next_drawable();

private:
    Device&         device_;
    CA::MetalLayer* layer_ = nullptr;
};

}  // namespace mge::renderer::metal
