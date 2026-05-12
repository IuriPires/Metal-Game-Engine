#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace mge::platform {

struct WindowDesc {
    std::string title       = "MetalGameEngine";
    std::uint32_t width     = 1280;
    std::uint32_t height    = 720;
    bool          resizable = true;
    bool          hi_dpi    = true;
};

// Top-level OS window with a CAMetalLayer-backed content view. The platform
// exposes the layer as `void*` to keep the public API free of Metal types;
// the RHI Device accepts the same opaque pointer to bind a swapchain.
class Window {
public:
    explicit Window(const WindowDesc& desc);
    ~Window();

    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&)                 = delete;
    Window& operator=(Window&&)      = delete;

    [[nodiscard]] bool should_close() const noexcept;
    void               request_close() noexcept;

    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;
    [[nodiscard]] std::uint32_t drawable_width() const noexcept;
    [[nodiscard]] std::uint32_t drawable_height() const noexcept;

    // Native layer pointer. On macOS this is a CAMetalLayer*. Pass to
    // mge::rhi::Device::create_swapchain.
    [[nodiscard]] void* native_layer() noexcept;

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace mge::platform
