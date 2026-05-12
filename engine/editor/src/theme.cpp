#include "theme.h"

namespace mge::editor {

namespace {
    float   g_dpi_scale = 1.0f;
    FontSet g_fonts;
}

float current_dpi_scale() noexcept { return g_dpi_scale; }

const FontSet& fonts() noexcept { return g_fonts; }

FontSet load_fonts(float dpi_scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    g_fonts = {};

    // macOS ships SF Pro + SF Mono at standard paths since Big Sur. They are
    // the system equivalents of Inter + JetBrains Mono respectively (the
    // design system reference fonts). Loading them avoids vendoring TTFs.
    constexpr const char* kSansPath = "/System/Library/Fonts/SFNS.ttf";
    constexpr const char* kMonoPath = "/System/Library/Fonts/SFNSMono.ttf";

    // Base size = 13 px logical (≈ Inter 12 from the design once subpixel
    // positioning kicks in). Multiplied by DPI scale so the font is sharp at
    // physical-pixel size on Retina.
    const float sans_size = 13.0f * (dpi_scale > 1.0f ? dpi_scale : 1.0f);
    const float mono_size = 12.0f * (dpi_scale > 1.0f ? dpi_scale : 1.0f);

    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH  = false;

    g_fonts.sans = io.Fonts->AddFontFromFileTTF(kSansPath, sans_size, &cfg);
    g_fonts.mono = io.Fonts->AddFontFromFileTTF(kMonoPath, mono_size, &cfg);

    if (g_fonts.sans == nullptr) {
        // System path missing — fall back to ImGui's built-in bitmap font.
        io.Fonts->AddFontDefault();
    }
    // No io.Fonts->Build() — the new ImGui Metal backend (docking HEAD)
    // lazy-builds the atlas the first time it's needed. Calling Build() now
    // triggers an assert because RendererHasTextures isn't set yet.

    // FontGlobalScale already accounted for via per-font size; reset it so
    // we don't double-scale.
    io.FontGlobalScale = 1.0f;

    return g_fonts;
}

void apply_theme(float dpi_scale) {
    g_dpi_scale = dpi_scale;
    using namespace tokens;
    ImGuiStyle& s = ImGui::GetStyle();

    // —— Geometry —— (0–2 px corners, 1 px borders, no shadows).
    s.WindowRounding    = r_0;
    s.ChildRounding     = r_0;
    s.PopupRounding     = r_1;
    s.FrameRounding     = r_1;
    s.GrabRounding      = r_0;
    s.ScrollbarRounding = r_0;
    s.TabRounding       = r_1;

    s.WindowBorderSize  = 1.0f;
    s.ChildBorderSize   = 1.0f;
    s.FrameBorderSize   = 1.0f;
    s.PopupBorderSize   = 1.0f;
    s.TabBorderSize     = 0.0f;

    // No drop shadows on widgets per the design brief — but the docking
    // backdrop wants none either.
#ifdef IMGUI_HAS_SHADOWS
    s.WindowShadowSize  = 0.0f;
    s.PopupShadowSize   = 0.0f;
#endif

    s.WindowPadding     = ImVec2(sp_4, sp_3);
    s.FramePadding      = ImVec2(sp_3, sp_1);
    s.CellPadding       = ImVec2(sp_3, sp_1);
    s.ItemSpacing       = ImVec2(sp_3, sp_1);
    s.ItemInnerSpacing  = ImVec2(sp_3, sp_2);
    s.IndentSpacing     = 14.0f;
    s.ScrollbarSize     = 10.0f;
    s.GrabMinSize       = 10.0f;

    s.WindowTitleAlign  = ImVec2(0.0f, 0.5f);
    s.WindowMenuButtonPosition = ImGuiDir_None;

    // —— Colors —— translated from styles.css :root.
    auto& C = s.Colors;
    C[ImGuiCol_Text]                  = col(fg_1);
    C[ImGuiCol_TextDisabled]          = col(fg_4);

    C[ImGuiCol_WindowBg]              = col(bg_1);
    C[ImGuiCol_ChildBg]               = col(bg_1);
    C[ImGuiCol_PopupBg]               = col(bg_2);

    C[ImGuiCol_Border]                = col(bd_1);
    C[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);

    C[ImGuiCol_FrameBg]               = col(bg_2);
    C[ImGuiCol_FrameBgHovered]        = col(bg_3);
    C[ImGuiCol_FrameBgActive]         = col(bg_4);

    C[ImGuiCol_TitleBg]               = col(bg_1);
    C[ImGuiCol_TitleBgActive]         = col(bg_1);
    C[ImGuiCol_TitleBgCollapsed]      = col(bg_1);

    C[ImGuiCol_MenuBarBg]             = col(bg_1);

    C[ImGuiCol_ScrollbarBg]           = col(bg_1);
    C[ImGuiCol_ScrollbarGrab]         = col(bd_2);
    C[ImGuiCol_ScrollbarGrabHovered]  = col(bd_3);
    C[ImGuiCol_ScrollbarGrabActive]   = col(fg_4);

    // Amber gets used for actionable affordances. Default checkmark / slider
    // grab uses the dim amber so the bright accent is reserved for "active"
    // selections (tree rows, focused inputs).
    C[ImGuiCol_CheckMark]             = col(acc);
    C[ImGuiCol_SliderGrab]            = col(acc_dim);
    C[ImGuiCol_SliderGrabActive]      = col(acc);

    C[ImGuiCol_Button]                = col(bg_2);
    C[ImGuiCol_ButtonHovered]         = col(bg_3);
    C[ImGuiCol_ButtonActive]          = col(bg_4);

    C[ImGuiCol_Header]                = col(acc_bg);
    C[ImGuiCol_HeaderHovered]         = col(bg_3);
    C[ImGuiCol_HeaderActive]          = col(acc_bg_2);

    C[ImGuiCol_Separator]             = col(bd_1);
    C[ImGuiCol_SeparatorHovered]      = col(bd_3);
    C[ImGuiCol_SeparatorActive]       = col(acc);

    C[ImGuiCol_ResizeGrip]            = col(bd_1);
    C[ImGuiCol_ResizeGripHovered]     = col(bd_3);
    C[ImGuiCol_ResizeGripActive]      = col(acc);

    C[ImGuiCol_Tab]                   = col(bg_1);
    C[ImGuiCol_TabHovered]            = col(bg_3);
    C[ImGuiCol_TabActive]             = col(bg_2);
    C[ImGuiCol_TabUnfocused]          = col(bg_1);
    C[ImGuiCol_TabUnfocusedActive]    = col(bg_2);

    C[ImGuiCol_DockingPreview]        = col(acc_bg_2);
    C[ImGuiCol_DockingEmptyBg]        = col(bg_0);

    C[ImGuiCol_TableHeaderBg]         = col(bg_1);
    C[ImGuiCol_TableBorderStrong]     = col(bd_1);
    C[ImGuiCol_TableBorderLight]      = col(bd_1);
    C[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
    C[ImGuiCol_TableRowBgAlt]         = col(bg_2);

    C[ImGuiCol_TextSelectedBg]        = col(acc_bg_2);
    C[ImGuiCol_DragDropTarget]        = col(acc);
    C[ImGuiCol_NavHighlight]          = col(acc);
    C[ImGuiCol_NavWindowingHighlight] = col(acc);
    C[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);

    // PlotLines / PlotHistogram (used by the profiler panel later).
    C[ImGuiCol_PlotLines]             = col(acc_dim);
    C[ImGuiCol_PlotLinesHovered]      = col(acc);
    C[ImGuiCol_PlotHistogram]         = col(acc_dim);
    C[ImGuiCol_PlotHistogramHovered]  = col(acc);

    // —— DPI scaling —— ScaleAllSizes multiplies every spacing/padding/size
    // value already set above. Calling it ONCE at theme apply time means we
    // get sharp, properly-sized chrome on Retina without rewriting every
    // numeric token. Fonts are loaded separately at the scaled size (see
    // load_fonts()), so FontGlobalScale stays at 1.0.
    if (dpi_scale > 1.001f) {
        s.ScaleAllSizes(dpi_scale);
    }
}

}  // namespace mge::editor
