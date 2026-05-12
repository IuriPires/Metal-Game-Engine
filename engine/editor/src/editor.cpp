#include "mge/editor/editor.h"

#include "chrome.h"
#include "imgui_platform.h"
#include "theme.h"

#include "imgui.h"

namespace mge::editor {

std::unique_ptr<Editor> Editor::create(rhi::Device& device,
                                        void* native_window,
                                        rhi::PixelFormat /*backbuffer_fmt*/,
                                        float dpi_scale) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename  = nullptr;  // Phase 1.5: don't persist layouts to disk yet.

    apply_theme(dpi_scale);
    (void)load_fonts(dpi_scale);  // result stored in theme.cpp static

    if (!platform::init_metal(device.native())) {
        ImGui::DestroyContext();
        return nullptr;
    }
    if (!platform::init_osx(native_window)) {
        platform::shutdown_metal();
        ImGui::DestroyContext();
        return nullptr;
    }

    auto e = std::unique_ptr<Editor>(new Editor);
    e->native_window_ = native_window;
    e->metal_device_  = device.native();
    return e;
}

Editor::~Editor() {
    platform::shutdown_osx();
    platform::shutdown_metal();
    ImGui::DestroyContext();
}

void Editor::render(const EngineState& state, rhi::RenderEncoder& enc,
                     void* command_buffer_native,
                     void* color_texture_native,
                     std::uint32_t drawable_width,
                     std::uint32_t drawable_height) {
    if (!visible_) return;

    platform::new_frame(native_window_,
                         drawable_width, drawable_height,
                         color_texture_native,
                         /*depth_texture=*/nullptr);
    ImGui::NewFrame();
    draw_chrome(state, selection_);
    ImGui::Render();
    platform::render_into_encoder(command_buffer_native, enc.native());
}

}  // namespace mge::editor
