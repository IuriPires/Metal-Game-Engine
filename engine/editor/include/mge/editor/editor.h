#pragma once

// M18 — Dear ImGui-based editor. The editor is a singleton-style object that
// owns the ImGui context, the Metal backend state, and the design-system
// theme. The frame lifecycle is split across two hooks so the FrameGraph can
// drive the actual encoding:
//
//   1. begin_frame()      — called once per frame, before any FG pass runs;
//                            polls input, advances ImGui's internal time.
//   2. build_ui(state)    — called after all engine state has been computed
//                            for the frame (sphere LOD, profiler snapshot,
//                            scene composition...). Draws every editor panel.
//   3. render_pass(...)   — encoded inside the editor's FG pass; submits the
//                            ImGui draw data to a render command encoder
//                            writing to the backbuffer with LoadAction::Load.
//
// The editor is opt-in: instantiate it only when `--editor` is passed.

#include "mge/rhi/device.h"
#include "mge/rhi/encoder.h"

#include <memory>
#include <string>
#include <vector>

namespace mge::platform { class Window; }
namespace mge::profile  { struct ZoneStats; }

namespace mge::editor {

// Compact view of per-frame engine state that the editor reads to populate
// the HUD lines, profiler bars, render-settings toggles, etc. Pass by const
// reference each frame.
struct EngineState {
    // Frame stats
    float       fps_now       = 0.0f;
    float       ms_last       = 0.0f;
    float       ms_avg        = 0.0f;
    std::uint64_t frame       = 0;
    double      sim_time      = 0.0;
    std::uint64_t step_count  = 0;

    // Scene counters
    std::uint32_t cubes_total       = 0;
    std::uint32_t cubes_frustum_vis = 0;
    std::uint32_t hzb_occluded      = 0;
    std::uint32_t particles         = 0;

    // LOD distribution (high / mid / low counts)
    std::uint32_t sphere_lod[3]     = {0, 0, 0};

    // Toggle states (the editor mirrors them, doesn't own them — pointer back
    // to the demo's bool so the editor can write through it).
    bool* rt_enabled       = nullptr;
    bool* hzb_enabled      = nullptr;
    bool* overlay_enabled  = nullptr;
    bool* demo_mode        = nullptr;

    // Profiler snapshot (already collected by mge::profile).
    const std::vector<profile::ZoneStats>* cpu_zones = nullptr;
};

class Editor {
public:
    // Create the editor over an existing RHI device + native NSWindow. The
    // backbuffer format must match the swapchain so the editor's render
    // pipeline can target it directly. `dpi_scale` is
    // `drawable_size / logical_size` for the window (2.0 on Retina) — every
    // explicit pixel size in the design system is multiplied by it.
    [[nodiscard]] static std::unique_ptr<Editor> create(
        rhi::Device& device, void* native_window, rhi::PixelFormat backbuffer_fmt,
        float dpi_scale = 1.0f);

    ~Editor();
    Editor(const Editor&)            = delete;
    Editor& operator=(const Editor&) = delete;

    // Single per-frame entry point. Runs the full ImGui frame:
    //   new frame → build chrome → render → encode draw data.
    // Caller owns the render encoder (LoadAction::Load on the backbuffer);
    // `color_texture_native` is the id<MTLTexture> of the bound color
    // attachment (ImGui's Metal backend needs it to derive the pixel format
    // + rasterSampleCount for its own pipeline state).
    void render(const EngineState&   state,
                rhi::RenderEncoder&  enc,
                void*                command_buffer_native,
                void*                color_texture_native,
                std::uint32_t        drawable_width,
                std::uint32_t        drawable_height);

    // Visibility (toggled by F1 in the demo).
    [[nodiscard]] bool visible() const noexcept { return visible_; }
    void               set_visible(bool v) noexcept { visible_ = v; }

    // Internal: lets the platform shim forward NSEvents.
    [[nodiscard]] void* native_window() const noexcept { return native_window_; }

private:
    Editor() = default;
    bool        visible_       = true;
    void*       native_window_ = nullptr;
    void*       metal_device_  = nullptr;
};

}  // namespace mge::editor
