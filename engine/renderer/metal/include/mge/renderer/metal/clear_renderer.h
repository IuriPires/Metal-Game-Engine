#pragma once

#include "mge/renderer/metal/clear_color.h"
#include "mge/renderer/metal/metal_cpp.h"

namespace mge::renderer::metal {

class Device;
class Swapchain;

// The simplest renderer in the engine: clears the current drawable to a
// configurable color, presents, returns. Used by the M1 demo and as a
// sanity check that the platform → swapchain → command queue chain is
// wired correctly. Removed (or absorbed) when M3 introduces the RHI.
class ClearRenderer {
public:
    ClearRenderer(Device& device, Swapchain& swapchain) noexcept;

    void set_clear_color(ClearColor c) noexcept { clear_ = c; }
    [[nodiscard]] ClearColor clear_color() const noexcept { return clear_; }

    // Encode + commit one frame. Returns true if a drawable was acquired and
    // a frame was submitted; false if the swapchain had no drawable available
    // this frame (caller should treat it as a dropped frame, not an error).
    bool draw();

private:
    Device&    device_;
    Swapchain& swapchain_;
    ClearColor clear_{0.05, 0.07, 0.10, 1.0};
};

}  // namespace mge::renderer::metal
