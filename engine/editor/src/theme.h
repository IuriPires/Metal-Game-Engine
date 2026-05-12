#pragma once

#include "imgui.h"

#include <cstdint>

namespace mge::editor {

// Design tokens lifted 1:1 from notes/design_drop/game-engine/project/styles.css.
// Keep this file in sync if the design system changes. Colors are RGBA8.
// Names match the CSS variable names (bg-0..bg-5, bd-1..bd-3, fg-1..fg-5,
// acc / ok / warn / err / info, axis-x/y/z).
namespace tokens {

constexpr ImU32 bg_0 = IM_COL32(0x0D, 0x10, 0x15, 0xFF);
constexpr ImU32 bg_1 = IM_COL32(0x12, 0x16, 0x1E, 0xFF);
constexpr ImU32 bg_2 = IM_COL32(0x18, 0x1D, 0x27, 0xFF);
constexpr ImU32 bg_3 = IM_COL32(0x1F, 0x25, 0x30, 0xFF);
constexpr ImU32 bg_4 = IM_COL32(0x26, 0x2D, 0x3A, 0xFF);
constexpr ImU32 bg_5 = IM_COL32(0x2F, 0x37, 0x44, 0xFF);

constexpr ImU32 bd_1 = IM_COL32(0x23, 0x2A, 0x36, 0xFF);
constexpr ImU32 bd_2 = IM_COL32(0x2C, 0x34, 0x41, 0xFF);
constexpr ImU32 bd_3 = IM_COL32(0x3A, 0x43, 0x52, 0xFF);

constexpr ImU32 fg_1 = IM_COL32(0xD8, 0xDD, 0xE6, 0xFF);
constexpr ImU32 fg_2 = IM_COL32(0xA4, 0xAB, 0xBA, 0xFF);
constexpr ImU32 fg_3 = IM_COL32(0x7E, 0x86, 0x96, 0xFF);
constexpr ImU32 fg_4 = IM_COL32(0x5A, 0x62, 0x73, 0xFF);
constexpr ImU32 fg_5 = IM_COL32(0x3F, 0x46, 0x54, 0xFF);

constexpr ImU32 acc      = IM_COL32(0xE8, 0xA2, 0x4A, 0xFF);
constexpr ImU32 acc_dim  = IM_COL32(0xB9, 0x7E, 0x35, 0xFF);
constexpr ImU32 acc_bg   = IM_COL32(0xE8, 0xA2, 0x4A, 0x1F);  // ~12% alpha
constexpr ImU32 acc_bg_2 = IM_COL32(0xE8, 0xA2, 0x4A, 0x38);  // ~22% alpha

constexpr ImU32 ok   = IM_COL32(0x73, 0xC2, 0x6A, 0xFF);
constexpr ImU32 warn = IM_COL32(0xE8, 0x9A, 0x3A, 0xFF);
constexpr ImU32 err  = IM_COL32(0xE0, 0x62, 0x5A, 0xFF);
constexpr ImU32 info = IM_COL32(0x4E, 0x9B, 0xE6, 0xFF);

constexpr ImU32 axis_x = err;   // saturated R for X (NOT pastel)
constexpr ImU32 axis_y = ok;    // saturated G for Y
constexpr ImU32 axis_z = info;  // saturated B for Z

// Sizing tokens, also from styles.css.
constexpr float menubar_h     = 28.0f;
constexpr float toolbar_h     = 36.0f;
constexpr float statusbar_h   = 22.0f;
constexpr float panel_hdr_h   = 28.0f;
constexpr float tree_row_h    = 22.0f;
constexpr float input_h       = 22.0f;
constexpr float button_h      = 24.0f;
constexpr float left_panel_w  = 280.0f;
constexpr float right_panel_w = 320.0f;
constexpr float dock_bottom_h = 280.0f;

// Spacing scale (sp-1..sp-8).
constexpr float sp_1 = 2.0f;
constexpr float sp_2 = 4.0f;
constexpr float sp_3 = 6.0f;
constexpr float sp_4 = 8.0f;
constexpr float sp_5 = 12.0f;
constexpr float sp_6 = 16.0f;
constexpr float sp_7 = 20.0f;
constexpr float sp_8 = 24.0f;

// Radius scale.
constexpr float r_0 = 0.0f;
constexpr float r_1 = 2.0f;
constexpr float r_2 = 3.0f;
constexpr float r_3 = 4.0f;

}  // namespace tokens

// Configure ImGuiStyle::Colors + ImGuiStyleVar_* from the design tokens.
// `dpi_scale` is `drawable_size / logical_size` on the parent window (2.0 on
// Retina). All size tokens and the font global scale are multiplied by it so
// the editor renders at physical-pixel size on HiDPI displays.
void apply_theme(float dpi_scale = 1.0f);

// Runtime DPI scale set by apply_theme(). Use `s(v)` to scale any explicit
// pixel-size token (panel heights, child window sizes). The values in the
// `tokens::` namespace stay at their CSS-spec (logical) sizes.
[[nodiscard]] float current_dpi_scale() noexcept;
[[nodiscard]] inline float s(float v) noexcept { return v * current_dpi_scale(); }

// Loaded once at editor startup; both are nullptr if the system fonts are
// missing (M18 fallback used the ImGui default bitmap font). The chrome
// pushes `font_mono` for any numeric / data / readout text; everything else
// uses the active default (`font_sans`).
struct FontSet {
    ImFont* sans = nullptr;
    ImFont* mono = nullptr;
};

[[nodiscard]] FontSet load_fonts(float dpi_scale);
[[nodiscard]] const FontSet& fonts() noexcept;

// RAII helper: push `font_mono` for the lifetime of this object.
class ScopedMonoFont {
public:
    ScopedMonoFont() {
        if (auto* f = fonts().mono) { ImGui::PushFont(f); pushed_ = true; }
    }
    ~ScopedMonoFont() { if (pushed_) ImGui::PopFont(); }
    ScopedMonoFont(const ScopedMonoFont&)            = delete;
    ScopedMonoFont& operator=(const ScopedMonoFont&) = delete;
private:
    bool pushed_ = false;
};

// Convert a packed RGBA8 ImU32 to an ImVec4 the ImGui colors table expects.
[[nodiscard]] inline ImVec4 col(ImU32 c) {
    const float r = ((c >> IM_COL32_R_SHIFT) & 0xFFu) / 255.0f;
    const float g = ((c >> IM_COL32_G_SHIFT) & 0xFFu) / 255.0f;
    const float b = ((c >> IM_COL32_B_SHIFT) & 0xFFu) / 255.0f;
    const float a = ((c >> IM_COL32_A_SHIFT) & 0xFFu) / 255.0f;
    return ImVec4(r, g, b, a);
}

}  // namespace mge::editor
