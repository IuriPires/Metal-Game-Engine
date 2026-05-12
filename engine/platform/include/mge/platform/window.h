#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace CA {
class MetalLayer;
}

namespace mge::platform {

struct WindowDesc {
    std::string title       = "MetalGameEngine";
    std::uint32_t width     = 1280;
    std::uint32_t height    = 720;
    bool          resizable = true;
    bool          hi_dpi    = true;
};

// Top-level OS window with a CAMetalLayer-backed content view. Phase 1
// only exposes what the M1 demo needs: a CA::MetalLayer pointer for the
// renderer, a close flag, and basic size queries.
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

    [[nodiscard]] CA::MetalLayer* metal_layer() noexcept;

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace mge::platform
