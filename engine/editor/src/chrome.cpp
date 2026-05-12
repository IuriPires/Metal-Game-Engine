#include "chrome.h"
#include "theme.h"

#include "imgui.h"
#include "imgui_internal.h"

#include "mge/core/version.h"
#include "mge/profile/profiler.h"

#include <algorithm>
#include <string>

#include <cstdio>
#include <string>

namespace mge::editor {

namespace {

// Helper: draw a fixed-height bar at the top of the current ImGui window,
// using the supplied background color and a 1 px bottom divider in bd_1.
void begin_chrome_bar(const char* id, float height, ImU32 bg) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(s(tokens::sp_4), 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(s(tokens::sp_2), 0.0f));
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
    const float  dot = s(6.0f);
    const float  y   = p.y + ImGui::GetTextLineHeight() * 0.5f - dot * 0.5f;
    draw->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + dot, y + dot),
                         tokens::acc);
    ImGui::Dummy(ImVec2(dot + s(tokens::sp_3), ImGui::GetTextLineHeight()));
    ImGui::SameLine(0.0f, s(tokens::sp_2));
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::acc));
    ImGui::TextUnformatted("METAL ENGINE");
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, s(tokens::sp_2));
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_3));
    const auto v = mge::core::engine_version();
    ImGui::Text("v%u.%u.%u", v.major, v.minor, v.patch);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, s(tokens::sp_4));
}

void menu_item(const char* label) {
    // Match .menubar .m-item: 12 px, 4×8 padding, 2 px radius, hover bg-3.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(tokens::sp_4), 4.0f));
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(tokens::bg_3));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  col(tokens::bg_4));
    ImGui::Button(label);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    ImGui::SameLine(0.0f, s(tokens::sp_1));
}

void meta_kv(const char* k, const char* v, ImU32 v_col = tokens::fg_2) {
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_4));
    ImGui::TextUnformatted(k);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, s(tokens::sp_2));
    ImGui::PushStyleColor(ImGuiCol_Text, col(v_col));
    {
        ScopedMonoFont _;     // data values render in monospace
        ImGui::TextUnformatted(v);
    }
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, s(tokens::sp_5));
}

// Render-only "readout" pill: framed key + value, monospace, used in the
// toolbar (.tb-readout in the design).
void readout(const char* k, const char* v, ImU32 v_col = tokens::fg_1) {
    ImGui::PushStyleColor(ImGuiCol_Border,  col(tokens::bd_2));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, col(tokens::bg_2));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    ImVec2(s(tokens::sp_4), 4.0f));

    // Pre-measure with the mono font so the pill auto-sizes to its contents.
    if (auto* mf = fonts().mono) ImGui::PushFont(mf);
    const ImVec2 text_size = ImGui::CalcTextSize(k);
    const float  v_size    = ImGui::CalcTextSize(v).x;
    if (fonts().mono) ImGui::PopFont();
    const float  width     = text_size.x + v_size + s(tokens::sp_4) * 2.0f + s(tokens::sp_3);

    ImGui::PushID(k);
    ImGui::BeginChild("##rd", ImVec2(width, s(tokens::button_h)),
                       ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeY,
                       ImGuiWindowFlags_NoScrollbar);
    {
        // ImGui requires Push/Pop to balance within a window — keep the
        // ScopedMonoFont lifetime strictly inside BeginChild/EndChild.
        ScopedMonoFont _;
        ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_4));
        ImGui::TextUnformatted(k);
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, s(tokens::sp_3));
        ImGui::PushStyleColor(ImGuiCol_Text, col(v_col));
        ImGui::TextUnformatted(v);
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::PopID();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImGui::SameLine(0.0f, s(tokens::sp_2));
}

void draw_menubar() {
    begin_chrome_bar("##menubar", s(tokens::menubar_h), tokens::bg_1);
    draw_brand();

    const char* items[] = {"File", "Edit", "Scene", "View", "Render",
                            "Tools", "Window", "Help"};
    for (const char* it : items) menu_item(it);

    // Right-aligned meta block.
    const float right_w = s(480.0f);
    ImGui::SameLine(ImGui::GetWindowWidth() - right_w);
    meta_kv("scene", "procedural_demo.scn");
    meta_kv("build", "Release · arm64");
    meta_kv("gpu",   "M-series");
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::ok));
    ImGui::TextUnformatted("* connected");  // M19 swaps for the proper bullet
    ImGui::PopStyleColor();

    end_chrome_bar(tokens::bd_1);
}

void draw_toolbar(const EngineState& state) {
    begin_chrome_bar("##toolbar", s(tokens::toolbar_h), tokens::bg_1);

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
                              ImVec2(s(tokens::sp_4), 4.0f));
        if (ImGui::Button(gizmos[i])) gizmo = i;
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::SameLine(0.0f, s(tokens::sp_1));
    }

    ImGui::Dummy(ImVec2(s(tokens::sp_3), 0.0f));
    ImGui::SameLine(0.0f, s(tokens::sp_3));

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

    // Right-aligned "Run command" hint pill. M19 swaps the ASCII Cmd+K hint
    // for the proper ⌘K glyph once a TTF font with extended unicode loads.
    const float right_w = s(200.0f);
    ImGui::SameLine(ImGui::GetWindowWidth() - right_w);
    readout("Cmd+K", "Run command", tokens::fg_4);

    end_chrome_bar(tokens::bd_1);
}

void draw_statusbar(const EngineState& state) {
    begin_chrome_bar("##statusbar", s(tokens::statusbar_h), tokens::bg_0);
    // Mono lifetime must end before end_chrome_bar's EndChild — explicit
    // block scope keeps Push/Pop balanced within this child window.
  {
    ScopedMonoFont _;

    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_4));
    ImGui::TextUnformatted("branch");
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, s(tokens::sp_2));
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_2));
    ImGui::TextUnformatted("main");
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, s(tokens::sp_6));

    char buf[80];
    std::snprintf(buf, sizeof(buf), "cubes %u / %u",
                  state.cubes_frustum_vis, state.cubes_total);
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_2));
    ImGui::TextUnformatted(buf);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, s(tokens::sp_6));

    std::snprintf(buf, sizeof(buf), "hzb %u occ", state.hzb_occluded);
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_2));
    ImGui::TextUnformatted(buf);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, s(tokens::sp_6));

    std::snprintf(buf, sizeof(buf), "particles %u", state.particles);
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_2));
    ImGui::TextUnformatted(buf);
    ImGui::PopStyleColor();

    // Right side: connected indicator + step count.
    const float right_w = s(220.0f);
    ImGui::SameLine(ImGui::GetWindowWidth() - right_w);
    std::snprintf(buf, sizeof(buf), "steps %llu",
                  static_cast<unsigned long long>(state.step_count));
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_3));
    ImGui::TextUnformatted(buf);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, s(tokens::sp_5));
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::ok));
    ImGui::TextUnformatted("* live");  // M19 swaps for the proper bullet
    ImGui::PopStyleColor();
  }  // ScopedMonoFont _

    end_chrome_bar(tokens::bd_1);
}

// Panel header (.panel-hdr in styles.css): ALL-CAPS title in fg_2, optional
// count hint in mono fg_4, hairline bottom divider in bd_1.
void draw_panel_header(const char* title, const char* count_hint) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                          ImVec2(s(tokens::sp_4), 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(tokens::bg_1));
    ImGui::PushID(title);
    ImGui::BeginChild("##phdr", ImVec2(0.0f, s(tokens::panel_hdr_h)),
                       ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    const float pad_y = (s(tokens::panel_hdr_h) - ImGui::GetTextLineHeight()) * 0.5f;
    if (pad_y > 0.0f) ImGui::SetCursorPosY(pad_y);
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_2));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    if (count_hint && *count_hint) {
        ImGui::SameLine(0.0f, s(tokens::sp_3));
        ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_4));
        ScopedMonoFont _;
        ImGui::TextUnformatted(count_hint);
        ImGui::PopStyleColor();
    }
    const ImVec2 p0 = ImGui::GetWindowPos();
    const ImVec2 p1 = ImVec2(p0.x + ImGui::GetWindowWidth(),
                              p0.y + ImGui::GetWindowHeight());
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(p0.x, p1.y - 0.5f), ImVec2(p1.x, p1.y - 0.5f),
        tokens::bd_1, 1.0f);
    ImGui::EndChild();
    ImGui::PopID();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// ──── Outliner ──────────────────────────────────────────────────────────
// One tree row matching .tree-row in the design: 22 px high, hover bg_2,
// selected bg = acc_bg with a 2 px amber inset stripe on the left edge,
// optional mono badge floated right. The Selectable handles input + bg;
// label + badge are drawn directly on the draw list so they don't mutate
// the ImGui cursor (which would shift subsequent rows out of place).
bool tree_row(int depth, const char* label, bool selected,
               const char* badge = nullptr) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                          ImVec2(s(tokens::sp_2), s(tokens::sp_1)));
    ImGui::PushStyleColor(ImGuiCol_Header,        col(tokens::acc_bg));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, col(tokens::bg_2));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  col(tokens::acc_bg_2));

    const float row_h = s(tokens::tree_row_h);
    ImGui::PushID(label);
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 size   = ImVec2(ImGui::GetContentRegionAvail().x, row_h);
    // No AllowOverlap: it requires the previous-frame HoveredId to match,
    // which throws away the first mouse click on a row. We don't have
    // overlapping widgets inside the row anyway (labels + badges draw
    // through the DrawList), so plain Selectable is correct.
    const bool clicked  = ImGui::Selectable("##row", selected, 0, size);

    auto* dl = ImGui::GetWindowDrawList();

    // 2 px amber inset stripe on selected row.
    if (selected) {
        dl->AddRectFilled(cursor,
                           ImVec2(cursor.x + s(2.0f), cursor.y + row_h),
                           tokens::acc);
    }

    // Label — DrawList::AddText so we don't disturb the layout cursor.
    const float text_h   = ImGui::GetTextLineHeight();
    const float label_y  = cursor.y + (row_h - text_h) * 0.5f;
    const float label_x  = cursor.x + s(static_cast<float>(depth) * 14.0f + 6.0f);
    const ImU32 label_co = selected ? tokens::acc : tokens::fg_1;
    dl->AddText(ImVec2(label_x, label_y), label_co, label);

    // Optional mono badge chip floated to the right edge. Use the current
    // mono font but render at the logical text size (LegacySize × global
    // scale) so the chip stays inside the row at HiDPI.
    if (badge) {
        ImFont* mono = fonts().mono;
        const float fscale = ImGui::GetIO().FontGlobalScale;
        const float font_h = mono ? mono->LegacySize * fscale
                                   : ImGui::GetFontSize();
        const float text_w = mono
            ? mono->CalcTextSizeA(font_h, FLT_MAX, 0.0f, badge).x
            : ImGui::CalcTextSize(badge).x;
        const float chip_pad_x = s(tokens::sp_3);
        const float chip_pad_y = s(tokens::sp_1);
        const float chip_w = text_w + chip_pad_x * 2.0f;
        const float chip_h = font_h  + chip_pad_y * 2.0f;
        const float chip_x = cursor.x + size.x - chip_w - s(tokens::sp_4);
        const float chip_y = cursor.y + (row_h - chip_h) * 0.5f;

        const ImU32 chip_bg = selected
            ? IM_COL32(0xE8, 0xA2, 0x4A, 0x14)
            : tokens::bg_2;
        const ImU32 chip_bd = selected
            ? IM_COL32(0xE8, 0xA2, 0x4A, 0x4D)
            : tokens::bd_2;
        const ImU32 chip_fg = selected ? tokens::acc : tokens::fg_3;

        dl->AddRectFilled(ImVec2(chip_x, chip_y),
                           ImVec2(chip_x + chip_w, chip_y + chip_h),
                           chip_bg, s(tokens::r_1));
        dl->AddRect(ImVec2(chip_x, chip_y),
                     ImVec2(chip_x + chip_w, chip_y + chip_h),
                     chip_bd, s(tokens::r_1));
        if (mono) {
            dl->AddText(mono, font_h,
                         ImVec2(chip_x + chip_pad_x, chip_y + chip_pad_y),
                         chip_fg, badge);
        } else {
            dl->AddText(ImVec2(chip_x + chip_pad_x, chip_y + chip_pad_y),
                         chip_fg, badge);
        }
    }

    ImGui::PopID();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    return clicked;
}

void draw_outliner(const EngineState& state, Selection& sel) {
    draw_panel_header("OUTLINER", "7 roots * scene");

    ImGui::BeginChild("##otree", ImVec2(0, 0), ImGuiChildFlags_None,
                       ImGuiWindowFlags_None);

    if (tree_row(0, "procedural_demo", sel.kind == SelectionKind::Scene, "scene")) {
        sel.kind = SelectionKind::Scene;
    }
    if (tree_row(1, "Spheres",
                  sel.kind == SelectionKind::SpheresGroup, "5")) {
        sel.kind = SelectionKind::SpheresGroup;
    }
    for (std::uint32_t i = 0; i < state.sphere_count; ++i) {
        const bool selected = sel.kind == SelectionKind::Sphere && sel.index == i;
        const char* name = (state.spheres && state.spheres[i].name)
                            ? state.spheres[i].name
                            : "Sphere";
        if (tree_row(2, name, selected)) {
            sel.kind  = SelectionKind::Sphere;
            sel.index = i;
        }
    }

    char cubebadge[16];
    std::snprintf(cubebadge, sizeof(cubebadge), "%u", state.cubes_total);
    if (tree_row(1, "Cube Field",
                  sel.kind == SelectionKind::CubeField, cubebadge)) {
        sel.kind = SelectionKind::CubeField;
    }
    char pbadge[16];
    std::snprintf(pbadge, sizeof(pbadge), "%u", state.particles);
    if (tree_row(1, "Particle Emitter",
                  sel.kind == SelectionKind::ParticleEmitter, pbadge)) {
        sel.kind = SelectionKind::ParticleEmitter;
    }
    char tbadge[16];
    std::snprintf(tbadge, sizeof(tbadge), "%u bones", state.tube_bone_count);
    if (tree_row(1, "Skinned Tube",
                  sel.kind == SelectionKind::SkinnedTube, tbadge)) {
        sel.kind = SelectionKind::SkinnedTube;
    }
    if (tree_row(1, "Sun Light", sel.kind == SelectionKind::SunLight)) {
        sel.kind = SelectionKind::SunLight;
    }
    if (tree_row(1, "Editor Camera", sel.kind == SelectionKind::Camera)) {
        sel.kind = SelectionKind::Camera;
    }

    ImGui::EndChild();
}

// ──── Inspector ─────────────────────────────────────────────────────────
void prop_row_begin(const char* label) {
    // 88 px label column + 1fr ctrl column per .prop in styles.css.
    constexpr float label_w_logical = 88.0f;
    ImGui::PushID(label);
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, s(label_w_logical));
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_3));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::NextColumn();
}
void prop_row_end() {
    ImGui::Columns(1);
    ImGui::PopID();
}

// Slider styled per .slider in the design — track fills with acc_bg_2 +
// dimmer right edge in acc_dim, numeric input on the right.
void styled_slider(const char* id, float* value, float vmin, float vmax,
                    const char* fmt = "%.2f") {
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, s(tokens::r_0));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        col(tokens::bg_2));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, col(tokens::bg_3));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  col(tokens::bg_3));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,     col(tokens::acc_bg_2));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, col(tokens::acc));
    ImGui::PushStyleColor(ImGuiCol_Text,           col(tokens::fg_1));
    ScopedMonoFont _;
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderFloat("##sl", value, vmin, vmax, fmt,
                        ImGuiSliderFlags_AlwaysClamp);
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar();
    ImGui::PopID();
}

void inspector_hero(const char* name, const char* path, ImU32 swatch,
                     bool is_sphere) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                          ImVec2(s(tokens::sp_5), s(tokens::sp_4)));
    ImGui::BeginChild("##insphero", ImVec2(0, s(54.0f)),
                       ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);

    auto* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float sw = s(36.0f);
    if (is_sphere) {
        dl->AddCircleFilled(ImVec2(p0.x + sw * 0.5f, p0.y + sw * 0.5f),
                              sw * 0.5f, swatch, 24);
        dl->AddCircle(ImVec2(p0.x + sw * 0.5f, p0.y + sw * 0.5f),
                       sw * 0.5f, tokens::bd_3, 24, 1.0f);
    } else {
        dl->AddRectFilled(p0, ImVec2(p0.x + sw, p0.y + sw), swatch);
        dl->AddRect(p0, ImVec2(p0.x + sw, p0.y + sw), tokens::bd_3);
    }

    ImGui::SetCursorScreenPos(ImVec2(p0.x + sw + s(tokens::sp_5), p0.y));
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_1));
    ImGui::TextUnformatted(name);
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(ImVec2(p0.x + sw + s(tokens::sp_5),
                                       p0.y + ImGui::GetTextLineHeight() + s(2.0f)));
    {
        ScopedMonoFont _;
        ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_3));
        ImGui::TextUnformatted(path);
        ImGui::PopStyleColor();
    }

    // Hairline divider on the bottom edge.
    const ImVec2 pp0 = ImGui::GetWindowPos();
    const ImVec2 pp1 = ImVec2(pp0.x + ImGui::GetWindowWidth(),
                                pp0.y + ImGui::GetWindowHeight());
    dl->AddLine(ImVec2(pp0.x, pp1.y - 0.5f),
                 ImVec2(pp1.x, pp1.y - 0.5f),
                 tokens::bd_1, 1.0f);

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void section(const char* title, bool* open) {
    ImGui::PushStyleColor(ImGuiCol_Header,        col(tokens::bg_1));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, col(tokens::bg_2));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  col(tokens::bg_2));
    ImGui::PushStyleColor(ImGuiCol_Text,          col(tokens::fg_2));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(tokens::sp_5), s(4.0f)));
    // ALL-CAPS section header per .section-hdr.
    ImGui::SetNextItemOpen(*open, ImGuiCond_Always);
    if (ImGui::CollapsingHeader(title)) {
        *open = true;
    } else {
        *open = false;
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
}

void draw_inspector_sphere(SphereView& sv, std::uint32_t idx) {
    const ImU32 swatch_col = sv.albedo
        ? IM_COL32(static_cast<int>(sv.albedo[0] * 255.0f),
                    static_cast<int>(sv.albedo[1] * 255.0f),
                    static_cast<int>(sv.albedo[2] * 255.0f), 255)
        : tokens::fg_3;
    char buf[32]; std::snprintf(buf, sizeof(buf), "k_spheres[%u]", idx);
    inspector_hero(sv.name ? sv.name : "Sphere", buf, swatch_col, true);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                          ImVec2(s(tokens::sp_5), s(tokens::sp_4)));
    ImGui::BeginChild("##inspbody", ImVec2(0, 0));

    static bool transform_open = true;
    static bool material_open  = true;

    section("TRANSFORM", &transform_open);
    if (transform_open) {
        ScopedMonoFont _;
        ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_2));
        ImGui::Text("position  %.2f, %.2f, %.2f",
                     static_cast<double>(sv.position[0]),
                     static_cast<double>(sv.position[1]),
                     static_cast<double>(sv.position[2]));
        ImGui::PopStyleColor();
    }

    section("MATERIAL", &material_open);
    if (material_open && sv.albedo) {
        prop_row_begin("albedo");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::ColorEdit3("##albedo", sv.albedo,
                            ImGuiColorEditFlags_NoInputs |
                            ImGuiColorEditFlags_AlphaPreviewHalf);
        prop_row_end();

        prop_row_begin("metallic");
        styled_slider("##metallic", sv.metallic, 0.0f, 1.0f, "%.2f");
        prop_row_end();

        prop_row_begin("roughness");
        styled_slider("##roughness", sv.roughness, 0.04f, 1.0f, "%.2f");
        prop_row_end();

        prop_row_begin("ao");
        styled_slider("##ao", sv.ao, 0.0f, 1.0f, "%.2f");
        prop_row_end();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void draw_inspector_empty(const char* line) {
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_4));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(tokens::sp_5), s(14.0f)));
    ImGui::TextWrapped("%s", line);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ──── Bottom dock panels ───────────────────────────────────────────────
void draw_console_panel() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                          ImVec2(s(tokens::sp_5), s(tokens::sp_3)));
    ImGui::BeginChild("##console.body", ImVec2(0, 0));
    {
        ScopedMonoFont _;
        // Placeholder log lines — M21 wires this to mge::core::Logger.
        struct Line { const char* lvl; ImU32 lvl_col; const char* msg; };
        static const Line lines[] = {
            {"info", tokens::fg_3,  "MetalGameEngine 0.0.1 (build=debug)"},
            {"info", tokens::fg_3,  "device: Apple M-series, instance_cap=1026  RT=yes"},
            {"info", tokens::fg_3,  "RT: on (TLAS over 1018 instances)"},
            {"info", tokens::fg_3,  "loop: sim 60Hz, target 120fps"},
            {"info", tokens::fg_3,  "editor: on"},
            {"info", tokens::fg_3,  "fonts: SFNS.ttf + SFNSMono.ttf @ 2x"},
            {"warn", tokens::warn,  "M21 will route real engine logs here."},
        };
        for (const auto& l : lines) {
            ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_4));
            ImGui::TextUnformatted("[ ]");
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, s(tokens::sp_3));
            ImGui::PushStyleColor(ImGuiCol_Text, col(l.lvl_col));
            ImGui::Text("%-4s", l.lvl);
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, s(tokens::sp_4));
            ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_1));
            ImGui::TextUnformatted(l.msg);
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void draw_profiler_panel(const EngineState& state) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                          ImVec2(s(tokens::sp_5), s(tokens::sp_4)));
    ImGui::BeginChild("##profiler.body", ImVec2(0, 0));

    if (!state.cpu_zones || state.cpu_zones->empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_4));
        ImGui::TextWrapped("No CPU zones recorded yet.");
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        return;
    }

    // Column header.
    const float col_name = s(180.0f);
    const float col_last = s(76.0f);
    const float col_avg  = s(76.0f);
    {
        ScopedMonoFont _;
        ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_4));
        ImGui::TextUnformatted("ZONE");
        ImGui::SameLine(col_name);
        ImGui::TextUnformatted("LAST ms");
        ImGui::SameLine(col_name + col_last);
        ImGui::TextUnformatted("AVG  ms");
        ImGui::SameLine(col_name + col_last + col_avg);
        ImGui::TextUnformatted("BAR (last vs 5 ms budget)");
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();

    // Find the max value across all zones for relative bar scaling.
    double max_ms = 5.0;     // hint a 5 ms frame-time budget
    for (const auto& z : *state.cpu_zones) {
        if (z.last_ms > max_ms) max_ms = z.last_ms;
        if (z.avg_ms  > max_ms) max_ms = z.avg_ms;
    }

    auto* dl = ImGui::GetWindowDrawList();
    for (const auto& z : *state.cpu_zones) {
        const float row_h = s(20.0f);
        const ImVec2 p   = ImGui::GetCursorScreenPos();

        {
            ScopedMonoFont _;
            ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_2));
            ImGui::TextUnformatted(std::string(z.name).c_str());
            ImGui::PopStyleColor();
        }
        ImGui::SameLine(col_name);
        {
            ScopedMonoFont _;
            ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_1));
            ImGui::Text("%6.3f", z.last_ms);
            ImGui::PopStyleColor();
        }
        ImGui::SameLine(col_name + col_last);
        {
            ScopedMonoFont _;
            ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_2));
            ImGui::Text("%6.3f", z.avg_ms);
            ImGui::PopStyleColor();
        }

        // Bar: last value as filled chunk, avg as a marker line.
        const float bar_x0 = p.x + col_name + col_last + col_avg;
        const float bar_x1 = p.x + ImGui::GetContentRegionAvail().x
                              + col_name + col_last + col_avg;
        const float bar_w  = std::max(0.0f, bar_x1 - bar_x0 - s(tokens::sp_4));
        const float track_h = s(8.0f);
        const float track_y = p.y + (row_h - track_h) * 0.5f;
        const float last_frac = static_cast<float>(z.last_ms / max_ms);
        const float avg_frac  = static_cast<float>(z.avg_ms  / max_ms);
        const float fill_w    = bar_w * last_frac;
        const float avg_x     = bar_x0 + bar_w * avg_frac;

        dl->AddRectFilled(ImVec2(bar_x0, track_y),
                            ImVec2(bar_x0 + bar_w, track_y + track_h),
                            tokens::bg_2);
        dl->AddRectFilled(ImVec2(bar_x0, track_y),
                            ImVec2(bar_x0 + fill_w, track_y + track_h),
                            tokens::acc_bg_2);
        dl->AddLine(ImVec2(bar_x0 + fill_w, track_y),
                     ImVec2(bar_x0 + fill_w, track_y + track_h),
                     tokens::acc, 1.0f);
        dl->AddLine(ImVec2(avg_x, track_y - s(2.0f)),
                     ImVec2(avg_x, track_y + track_h + s(2.0f)),
                     tokens::fg_4, 1.0f);

        ImGui::Dummy(ImVec2(0.0f, row_h - ImGui::GetTextLineHeight()));
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void draw_render_settings_panel(const EngineState& state) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                          ImVec2(s(tokens::sp_5), s(tokens::sp_4)));
    ImGui::BeginChild("##rs.body", ImVec2(0, 0));

    auto toggle_row = [](const char* label, bool* value) {
        prop_row_begin(label);
        if (value) {
            ImGui::Checkbox("##t", value);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_4));
            ImGui::TextUnformatted("—");
            ImGui::PopStyleColor();
        }
        prop_row_end();
    };

    static bool render_open  = true;
    static bool feature_open = true;

    section("FEATURES", &feature_open);
    if (feature_open) {
        toggle_row("editor",   nullptr);   // always-on while we're in the editor
        toggle_row("rt",       state.rt_enabled);
        toggle_row("hzb",      state.hzb_enabled);
        toggle_row("overlay",  state.overlay_enabled);
        toggle_row("demo cycle", state.demo_mode);
    }

    section("RENDER", &render_open);
    if (render_open) {
        ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_4));
        ImGui::TextWrapped("Sliders for sun / bloom / exposure / ambient land in M21 once the engine cvars are exposed mutably.");
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void draw_dock_placeholder(const char* tab) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                          ImVec2(s(tokens::sp_5), s(tokens::sp_4)));
    ImGui::PushStyleColor(ImGuiCol_Text, col(tokens::fg_4));
    ImGui::BeginChild("##phx", ImVec2(0, 0));
    ImGui::TextWrapped("%s panel wires up in M20.b.", tab);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void draw_inspector(const EngineState& state, Selection& sel) {
    draw_panel_header("INSPECTOR", "");
    switch (sel.kind) {
        case SelectionKind::Sphere:
            if (state.spheres && sel.index < state.sphere_count) {
                draw_inspector_sphere(state.spheres[sel.index], sel.index);
            } else {
                draw_inspector_empty("Sphere not available.");
            }
            break;
        case SelectionKind::None:
            draw_inspector_empty("Nothing selected.\nClick an item in the Outliner.");
            break;
        default:
            draw_inspector_empty("Inspector for this entity arrives in M19c.");
            break;
    }
}

}  // namespace

void draw_chrome(const EngineState& state, Selection& sel) {
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
                              - s(tokens::dock_bottom_h) - s(tokens::statusbar_h);
    const float center_w    = ImGui::GetContentRegionAvail().x
                              - s(tokens::left_panel_w) - s(tokens::right_panel_w);
    const ImVec2 panel_size = ImVec2(s(tokens::left_panel_w), total_h);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(tokens::bg_1));
    ImGui::BeginChild("##left", panel_size,
                       ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    draw_outliner(state, sel);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 0.0f);

    // Center column is transparent — the rasterized engine output shows
    // through unaltered. NoBackground on the child + transparent ChildBg.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border,  ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##center", ImVec2(center_w, total_h),
                       ImGuiChildFlags_None,
                       ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoBackground);
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::SameLine(0.0f, 0.0f);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(tokens::bg_1));
    ImGui::BeginChild("##right", ImVec2(s(tokens::right_panel_w), total_h),
                       ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    draw_inspector(state, sel);
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // Bottom dock — tab strip placeholder.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(tokens::bg_1));
    ImGui::BeginChild("##dock_bottom",
                       ImVec2(0.0f, s(tokens::dock_bottom_h)),
                       ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);

    // Tab row.
    static const char* tabs[] = {"Console", "Profiler", "FrameGraph",
                                  "Render Settings", "Shader Reload"};
    static int active_tab = 0;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                          ImVec2(s(tokens::sp_5), 6.0f));
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

    // Tab body — route by index.
    switch (active_tab) {
        case 0: draw_console_panel();                 break;
        case 1: draw_profiler_panel(state);           break;
        case 2: draw_dock_placeholder("FrameGraph");  break;
        case 3: draw_render_settings_panel(state);    break;
        case 4: draw_dock_placeholder("Shader Reload"); break;
        default: break;
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    draw_statusbar(state);

    ImGui::PopStyleVar();  // ItemSpacing
    ImGui::End();
}

}  // namespace mge::editor
