#include "mge/editor/editor.h"

#include "chrome.h"
#include "imgui_platform.h"
#include "theme.h"

#include "imgui.h"
#include "ImGuizmo.h"

#include <cstring>

namespace mge::editor {

namespace {

// Translate ImGuiKey enum values into our slim Key enum. Keep in sync with
// scene/input_state.h. Returns Key::Unknown for keys we don't track.
[[nodiscard]] scene::Key imgui_to_key(ImGuiKey k) noexcept {
    switch (k) {
        case ImGuiKey_W: return scene::Key::W;
        case ImGuiKey_A: return scene::Key::A;
        case ImGuiKey_S: return scene::Key::S;
        case ImGuiKey_D: return scene::Key::D;
        case ImGuiKey_Q: return scene::Key::Q;
        case ImGuiKey_E: return scene::Key::E;
        case ImGuiKey_F: return scene::Key::F;
        case ImGuiKey_F1:          return scene::Key::F1;
        case ImGuiKey_F2:          return scene::Key::F2;
        case ImGuiKey_Tab:         return scene::Key::Tab;
        case ImGuiKey_Escape:      return scene::Key::Esc;
        case ImGuiKey_Space:       return scene::Key::Space;
        case ImGuiKey_LeftBracket: return scene::Key::LeftBracket;
        case ImGuiKey_RightBracket:return scene::Key::RightBracket;
        case ImGuiKey_1:           return scene::Key::Digit1;
        case ImGuiKey_3:           return scene::Key::Digit3;
        case ImGuiKey_7:           return scene::Key::Digit7;
        default:                   return scene::Key::Unknown;
    }
}

void populate_input_state(scene::InputState& out, bool viewport_hovered) noexcept {
    const ImGuiIO& io = ImGui::GetIO();

    out.mouse_pos_x   = io.MousePos.x;
    out.mouse_pos_y   = io.MousePos.y;
    out.mouse_delta_x = io.MouseDelta.x;
    out.mouse_delta_y = io.MouseDelta.y;
    out.scroll_x      = io.MouseWheelH;
    out.scroll_y      = io.MouseWheel;

    // ImGui mouse buttons: 0=L, 1=R, 2=M. Our Button enum matches.
    for (std::size_t i = 0; i < scene::k_button_count; ++i) {
        const bool now  = ImGui::IsMouseDown(static_cast<int>(i));
        const bool was  = out.button_down[i];
        out.button_down[i]     = now;
        out.button_pressed[i]  = now && !was;
        out.button_released[i] = !now && was;
    }

    // Clear edge state for keys, then re-derive from current down state.
    std::array<bool, scene::k_key_count> prev_keys = out.key_down;
    std::memset(out.key_down.data(),    0, sizeof(out.key_down));
    std::memset(out.key_pressed.data(), 0, sizeof(out.key_pressed));
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        const auto our_k = imgui_to_key(static_cast<ImGuiKey>(k));
        if (our_k == scene::Key::Unknown) continue;
        const bool now = ImGui::IsKeyDown(static_cast<ImGuiKey>(k));
        const std::size_t idx = static_cast<std::size_t>(our_k);
        out.key_down[idx] = now;
        if (now && !prev_keys[idx]) out.key_pressed[idx] = true;
    }

    out.mods.shift = io.KeyShift;
    out.mods.ctrl  = io.KeyCtrl;
    out.mods.alt   = io.KeyAlt;
    out.mods.super = io.KeySuper;

    out.viewport_hovered = viewport_hovered;
    out.keyboard_focused = io.WantTextInput;
}

}  // namespace

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
    ImGuizmo::BeginFrame();
    bool hovered    = false;
    bool gizmo_used = false;
    // ImGuizmo::Manipulate runs inside draw_chrome, hosted in the central
    // viewport child (see chrome.cpp). That's the canonical pattern: the
    // gizmo uses that window's draw list + screen rect so its hit testing
    // matches what's drawn, instead of fighting with the transparent
    // center child for mouse capture.
    const int gizmo_op_int =
          gizmo_op_ == GizmoOp::Rotate ? 1
        : gizmo_op_ == GizmoOp::Scale  ? 2
        : 0;
    draw_chrome(state, selection_, &hovered, gizmo_op_int, &gizmo_used);
    viewport_hovered_ = hovered;
    gizmo_active_     = gizmo_used;

    populate_input_state(input_state_, viewport_hovered_ && !gizmo_active_);
    ImGui::Render();
    platform::render_into_encoder(command_buffer_native, enc.native());
}

}  // namespace mge::editor
