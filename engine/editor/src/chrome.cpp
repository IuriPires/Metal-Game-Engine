#include "chrome.h"
#include "theme.h"

#include "imgui.h"
#include "imgui_internal.h"

#include "mge/core/version.h"

#include <cstdio>
#include <string>

namespace mge::editor {

namespace {

// Helper: draw a fixed-height bar at the top of the current ImGui window,
// using the supplied background color and a 1 px bottom divider in bd_1.
void begin_chrome_bar(const char* id, float height, ImU32 bg) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tokens::sp_4, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(tokens::sp_2, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(bg));
    ImGui::BeginChild(id, ImVec2(0.0f, height),
                       ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    // Vertical center: nudge the cursor down by half the leftover row.
    const float text_h = ImGui::GetTextLineHeight();
    const float pad_y  = (height - text_h) * 0.5f;
    if (pad_y > 0.0f) ImGui::SetCursorPosY(pad_y);
}

void end_chrome_bar(ImU32 divider) {
    const ImVec2 p0 = ImGui::GetWindowPos();
    const ImVec2 p1 = ImVec2(p0.x + ImGui::GetWindowWidth(),
                              p0.y + ImGui::GetWindowHeight());
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(p0.x, p1.y - 0.5f),
        ImVec2(p1.x, p1.y - 0.5f),
        divider, 1.0f);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

// .menubar .brand: amber dot + monospace "METAL ENGINE v0.4.2".
void draw_brand() {
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float  y = p.y + ImGui::GetTextLineHeight() * 0.5f - 3.0f;
    draw->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + 6.0f, y + 6.0f),
                         tokens::acc);
    ImGui::Dummy(ImVec2(6.0f + tokens::sp_3, ImGui::GetTextLineHeight()));
    ImGui::SameLine(0.0f, tokens::sp_2);
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::acc));
    ImGui::TextUnformatted("METAL ENGINE");
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, tokens::sp_2);
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_3));
    const auto v = mge::core::engine_version();
    ImGui::Text("v%u.%u.%u", v.major, v.minor, v.patch);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, tokens::sp_4);
}

void menu_item(const char* label) {
    // Match .menubar .m-item: 12 px, 4×8 padding, 2 px radius, hover bg-3.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tokens::sp_4, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(tokens::bg_3));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  col(tokens::bg_4));
    ImGui::Button(label);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    ImGui::SameLine(0.0f, tokens::sp_1);
}

void meta_kv(const char* k, const char* v, ImU32 v_col = tokens::fg_2) {
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_4));
    ImGui::TextUnformatted(k);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, tokens::sp_2);
    ImGui::PushStyleColor(ImGuiCol_Text, col(v_col));
    ImGui::TextUnformatted(v);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, tokens::sp_5);
}

// Render-only "readout" pill: framed key + value, monospace, used in the
// toolbar (.tb-readout in the design).
void readout(const char* k, const char* v, ImU32 v_col = tokens::fg_1) {
    ImGui::PushStyleColor(ImGuiCol_Border,  col(tokens::bd_2));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, col(tokens::bg_2));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    ImVec2(tokens::sp_4, 4.0f));

    const ImVec2 text_size = ImGui::CalcTextSize(k);
    const float  v_size    = ImGui::CalcTextSize(v).x;
    const float  width     = text_size.x + v_size + tokens::sp_4 * 2.0f + tokens::sp_3;

    ImGui::PushID(k);
    ImGui::BeginChild("##rd", ImVec2(width, tokens::button_h),
                       ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeY,
                       ImGuiWindowFlags_NoScrollbar);
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_4));
    ImGui::TextUnformatted(k);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, tokens::sp_3);
    ImGui::PushStyleColor(ImGuiCol_Text, col(v_col));
    ImGui::TextUnformatted(v);
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopID();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImGui::SameLine(0.0f, tokens::sp_2);
}

void draw_menubar() {
    begin_chrome_bar("##menubar", tokens::menubar_h, tokens::bg_1);
    draw_brand();

    const char* items[] = {"File", "Edit", "Scene", "View", "Render",
                            "Tools", "Window", "Help"};
    for (const char* it : items) menu_item(it);

    // Right-aligned meta block.
    const float right_w = 480.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - right_w);
    meta_kv("scene", "procedural_demo.scn");
    meta_kv("build", "Release · arm64");
    meta_kv("gpu",   "M-series");
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::ok));
    ImGui::TextUnformatted("● connected");
    ImGui::PopStyleColor();

    end_chrome_bar(tokens::bd_1);
}

void draw_toolbar(const EngineState& state) {
    begin_chrome_bar("##toolbar", tokens::toolbar_h, tokens::bg_1);

    // Gizmo group: Translate / Rotate / Scale (stub buttons for M18; wire up
    // to real engine state in M19).
    static int gizmo = 0;
    const char* gizmos[] = {"Move", "Rot", "Scale"};
    for (int i = 0; i < 3; ++i) {
        const bool on = gizmo == i;
        ImGui::PushStyleColor(ImGuiCol_Button,
            col(on ? tokens::acc_bg : tokens::bg_2));
        ImGui::PushStyleColor(ImGuiCol_Text,
            col(on ? tokens::acc : tokens::fg_2));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                              ImVec2(tokens::sp_4, 4.0f));
        if (ImGui::Button(gizmos[i])) gizmo = i;
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::SameLine(0.0f, tokens::sp_1);
    }

    ImGui::Dummy(ImVec2(tokens::sp_3, 0.0f));
    ImGui::SameLine(0.0f, tokens::sp_3);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(state.ms_avg));
    readout("ms avg", buf, tokens::fg_1);

    std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(state.fps_now));
    readout("fps",  buf, tokens::acc);

    std::snprintf(buf, sizeof(buf), "%llu",
                  static_cast<unsigned long long>(state.frame));
    readout("frame", buf);

    std::snprintf(buf, sizeof(buf), "%.2fs", state.sim_time);
    readout("sim",   buf);

    // Right-aligned ⌘K hint, like the design's "Run command" pill.
    const float right_w = 200.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - right_w);
    readout("⌘K", "Run command", tokens::fg_4);

    end_chrome_bar(tokens::bd_1);
}

void draw_statusbar(const EngineState& state) {
    begin_chrome_bar("##statusbar", tokens::statusbar_h, tokens::bg_0);

    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_4));
    ImGui::TextUnformatted("branch");
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, tokens::sp_2);
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_2));
    ImGui::TextUnformatted("main");
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, tokens::sp_6);

    char buf[80];
    std::snprintf(buf, sizeof(buf), "cubes %u / %u",
                  state.cubes_frustum_vis, state.cubes_total);
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_2));
    ImGui::TextUnformatted(buf);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, tokens::sp_6);

    std::snprintf(buf, sizeof(buf), "hzb %u occ", state.hzb_occluded);
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_2));
    ImGui::TextUnformatted(buf);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, tokens::sp_6);

    std::snprintf(buf, sizeof(buf), "particles %u", state.particles);
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_2));
    ImGui::TextUnformatted(buf);
    ImGui::PopStyleColor();

    // Right side: connected indicator + step count.
    const float right_w = 220.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - right_w);
    std::snprintf(buf, sizeof(buf), "steps %llu",
                  static_cast<unsigned long long>(state.step_count));
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_3));
    ImGui::TextUnformatted(buf);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, tokens::sp_5);
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::ok));
    ImGui::TextUnformatted("● live");
    ImGui::PopStyleColor();

    end_chrome_bar(tokens::bd_1);
}

void draw_panel_placeholder(const char* title, const char* count_hint) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                          ImVec2(tokens::sp_4, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(tokens::bg_1));
    ImGui::BeginChild("##phdr", ImVec2(0.0f, tokens::panel_hdr_h),
                       ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    const float pad_y = (tokens::panel_hdr_h - ImGui::GetTextLineHeight()) * 0.5f;
    if (pad_y > 0.0f) ImGui::SetCursorPosY(pad_y);
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_2));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    if (count_hint && *count_hint) {
        ImGui::SameLine(0.0f, tokens::sp_3);
        ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_4));
        ImGui::TextUnformatted(count_hint);
        ImGui::PopStyleColor();
    }
    // 1 px bottom divider matches .panel-hdr in styles.css.
    const ImVec2 p0 = ImGui::GetWindowPos();
    const ImVec2 p1 = ImVec2(p0.x + ImGui::GetWindowWidth(),
                              p0.y + ImGui::GetWindowHeight());
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(p0.x, p1.y - 0.5f), ImVec2(p1.x, p1.y - 0.5f),
        tokens::bd_1, 1.0f);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // Body — M18 placeholder text. Real content arrives in M19+.
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_4));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tokens::sp_5, 14.0f));
    ImGui::TextWrapped("M18 placeholder. M19 wires this panel up.");
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

}  // namespace

void draw_chrome(const EngineState& state) {
    // Full-screen invisible window holding the entire editor chrome. ImGui's
    // viewport drives the size; we cover the swapchain.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags root_flags =
        ImGuiWindowFlags_NoTitleBar     | ImGuiWindowFlags_NoCollapse  |
        ImGuiWindowFlags_NoResize       | ImGuiWindowFlags_NoMove      |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground   | ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##mge_editor_root", nullptr, root_flags);
    ImGui::PopStyleVar(3);

    draw_menubar();
    draw_toolbar(state);

    // Three-column workarea: left (outliner), center (viewport), right (inspector).
    const float total_h     = ImGui::GetContentRegionAvail().y
                              - tokens::dock_bottom_h - tokens::statusbar_h;
    const float center_w    = ImGui::GetContentRegionAvail().x
                              - tokens::left_panel_w - tokens::right_panel_w;
    const ImVec2 panel_size = ImVec2(tokens::left_panel_w, total_h);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(tokens::bg_1));
    ImGui::BeginChild("##left", panel_size,
                       ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    draw_panel_placeholder("OUTLINER", "0 roots · 0 obj");
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 0.0f);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(tokens::bg_0));
    ImGui::BeginChild("##center", ImVec2(center_w, total_h),
                       ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    // The center is intentionally transparent so the rasterized engine output
    // shows through unaltered. We draw nothing here in M18.
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 0.0f);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(tokens::bg_1));
    ImGui::BeginChild("##right", ImVec2(tokens::right_panel_w, total_h),
                       ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    draw_panel_placeholder("INSPECTOR", "");
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // Bottom dock — tab strip placeholder.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(tokens::bg_1));
    ImGui::BeginChild("##dock_bottom",
                       ImVec2(0.0f, tokens::dock_bottom_h),
                       ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);

    // Tab row.
    static const char* tabs[] = {"Console", "Profiler", "FrameGraph",
                                  "Render Settings", "Shader Reload"};
    static int active_tab = 0;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                          ImVec2(tokens::sp_5, 6.0f));
    for (int i = 0; i < IM_ARRAYSIZE(tabs); ++i) {
        const bool on = active_tab == i;
        ImGui::PushStyleColor(ImGuiCol_Button,
            col(on ? tokens::bg_2 : tokens::bg_1));
        ImGui::PushStyleColor(ImGuiCol_Text,
            col(on ? tokens::acc : tokens::fg_3));
        if (ImGui::Button(tabs[i])) active_tab = i;
        ImGui::PopStyleColor(2);
        ImGui::SameLine(0.0f, 0.0f);
    }
    ImGui::PopStyleVar(2);
    ImGui::NewLine();

    // Tab body placeholder.
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_4));
    ImGui::SetCursorPos(ImVec2(tokens::sp_5, 48.0f));
    ImGui::Text("M18 placeholder · %s panel wires up in M19/M20.", tabs[active_tab]);
    ImGui::PopStyleColor();

    ImGui::EndChild();
    ImGui::PopStyleColor();

    draw_statusbar(state);

    ImGui::PopStyleVar();  // ItemSpacing
    ImGui::End();
}

}  // namespace mge::editor
