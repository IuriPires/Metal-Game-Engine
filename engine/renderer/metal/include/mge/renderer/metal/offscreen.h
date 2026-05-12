#pragma once

#include "mge/renderer/metal/clear_color.h"
#include "mge/renderer/metal/metal_cpp.h"

#include <array>
#include <cstdint>
#include <vector>

namespace mge::renderer::metal {

class Device;

// Self-contained offscreen target used by integration tests: allocate a small
// RGBA8 texture, run a clear pass on it, copy back to a CPU-readable buffer,
// expose the pixels. No window, no swapchain - runs on any Metal-capable
// machine including CI.
class Offscreen {
public:
    Offscreen(Device& device, unsigned width, unsigned height,
              MTL::PixelFormat fmt = MTL::PixelFormatRGBA8Unorm) noexcept;

    ~Offscreen();

    Offscreen(const Offscreen&)            = delete;
    Offscreen& operator=(const Offscreen&) = delete;

    [[nodiscard]] unsigned width() const noexcept { return width_; }
    [[nodiscard]] unsigned height() const noexcept { return height_; }
    [[nodiscard]] MTL::Texture* texture() noexcept { return texture_; }

    // Render a clear-only pass and read back. Returns the linear RGBA bytes
    // (row-major, top-down, 4 bytes per pixel). Blocks until GPU completes.
    [[nodiscard]] std::vector<std::uint8_t> clear_and_read(ClearColor c);

    // Single 4-byte pixel at (x, y). Convenience for tests.
    [[nodiscard]] std::array<std::uint8_t, 4>
        clear_and_read_pixel(ClearColor c, unsigned x, unsigned y);

private:
    Device&         device_;
    unsigned        width_   = 0;
    unsigned        height_  = 0;
    MTL::Texture*   texture_ = nullptr;
    MTL::Buffer*    readback_ = nullptr;
};

}  // namespace mge::renderer::metal
