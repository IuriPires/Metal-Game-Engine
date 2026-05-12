#pragma once

#include "mge/rhi/enums.h"
#include "mge/rhi/texture.h"

#include <cstdint>
#include <string>

namespace mge::rhi {

class Device;

// One-shot handle holding a drawable acquired from the swapchain. The texture
// is valid only during this frame; destroy or present before scope exits.
class SwapchainFrame {
public:
    ~SwapchainFrame();
    SwapchainFrame(const SwapchainFrame&)            = delete;
    SwapchainFrame& operator=(const SwapchainFrame&) = delete;
    SwapchainFrame(SwapchainFrame&&) noexcept;
    SwapchainFrame& operator=(SwapchainFrame&&) = delete;

    [[nodiscard]] bool     valid() const noexcept { return drawable_ != nullptr; }
    [[nodiscard]] Texture* texture() noexcept { return &texture_; }

    [[nodiscard]] void* native_drawable() noexcept { return drawable_; }

private:
    friend class Swapchain;
    friend class CommandBuffer;
    SwapchainFrame(void* drawable, Texture tex) noexcept
        : drawable_(drawable), texture_(std::move(tex)) {}

    void*   drawable_ = nullptr;
    Texture texture_{nullptr, TextureDesc{}, /*owned=*/false};
};

// Swapchain wraps a native layer (CAMetalLayer on Apple). Owned by the caller;
// platform-layer Window keeps the underlying native pointer alive.
class Swapchain {
public:
    ~Swapchain();
    Swapchain(const Swapchain&)            = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    void resize(std::uint32_t width, std::uint32_t height);
    [[nodiscard]] PixelFormat color_format() const noexcept { return format_; }
    void set_color_format(PixelFormat fmt);

    [[nodiscard]] SwapchainFrame acquire_frame();

    [[nodiscard]] void* native_layer() noexcept { return native_layer_; }

private:
    friend class Device;
    Swapchain(void* native_layer, Device* device, PixelFormat fmt) noexcept
        : native_layer_(native_layer), device_(device), format_(fmt) {}

    void*       native_layer_ = nullptr;
    Device*     device_       = nullptr;
    PixelFormat format_       = PixelFormat::BGRA8UnormSrgb;
};

}  // namespace mge::rhi
