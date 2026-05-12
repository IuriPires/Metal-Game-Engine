#pragma once

// Editor → platform shim header. The .mm implementation wraps ImGui's Metal
// + OSX backends. Pure-C++ TUs include this and call through; only
// imgui_platform.mm sees Cocoa / Metal types directly.

#include <cstdint>

namespace mge::editor::platform {

bool init_metal(void* native_mtl_device);
void shutdown_metal();

bool init_osx(void* native_nswindow);
void shutdown_osx();

// Called once per frame before any ImGui:: calls. Sets ImGui's display size
// and prepares the Metal backend with a fresh render-pass descriptor.
void new_frame(void*         native_nswindow,
                std::uint32_t drawable_width,
                std::uint32_t drawable_height,
                void*         color_texture,
                void*         depth_texture);

// Submit ImGui::GetDrawData() into the given render command encoder.
void render_into_encoder(void* command_buffer_native, void* encoder_native);

}  // namespace mge::editor::platform
