# ADR-0016 — Transform gizmos via ImGuizmo (Phase 2)

- **Status**: accepted
- **Date**: 2026-05-13
- **Deciders**: editor / renderer
- **Tags**: editor, gizmo, dependency, imgui

## Context

M27 lands viewport picking — click → select. The natural next step is
manipulation: drag handles to translate / rotate / scale the selected
entity. Every meaningful editor (Unity, Unreal, Godot, Maya, Blender, Hazel,
ImHex, RenderDoc) ships a 3-axis gizmo widget; a game engine without one
isn't an editor, it's a viewer.

We have a Dear ImGui front-end (ADR-0014). Drawing the gizmo geometry on
top of the rasterised scene wants `ImDrawList` access (immediate-mode line
strokes in screen space, anti-aliased, alpha-blended). Hit-testing wants
ImGui's `IsMouseDown` / `IsMouseClicked` and our `Editor::viewport_hovered`
gate (ADR-0015).

Three implementation paths considered.

## Decision

**Vendor [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo)** via
FetchContent (pinned SHA `ba662b1`, MIT licence, ~5 kLOC single-cpp). It
becomes the only gizmo we ship for Phase 2; a future custom replacement
isn't blocked but isn't planned.

`ImGuizmo::Manipulate(view, projection, OPERATION, MODE, model_matrix)` is
called inside the editor's `render()` between `draw_chrome()` and
`ImGui::Render()`. The demo points `EngineState::active_model` at the
selected entity's model matrix; the editor surfaces the current
`ImGuizmo::OPERATION` (translate / rotate / scale) controlled by the
W / E / R hotkeys.

## Alternatives considered

### A — Vendor ImGuizmo (chosen)

- **Pros**: industry standard for ImGui-based editors. Same maintainer as
  Dear ImGui-adjacent contributors. Handles translate / rotate / scale,
  world / local space, screen-space sizing, snap, and bounds-edit out of
  the box. ~5 kLOC, MIT, header + cpp, zero dependencies beyond Dear ImGui
  itself. Easy to drop in and easy to replace.
- **Cons**: another vendored dep (currently 7 incl. metal-cpp). Pulls a
  ~1.5 MB git checkout. Built-in styling defaults are functional but not
  on-brand — we override colours through `ImGuizmo::Style&`.

### B — Hand-rolled gizmo

- **Pros**: full control over visuals (amber-on-slate palette out of the
  box). No dep. Educational.
- **Cons**: ~500–800 lines for a competent 3-axis manipulator (project
  click ray onto axis, drag math with screen-space delta, hit-test for
  arrows / planes / centre, mode toggles, snap). Subtle correctness bugs
  in drag math eat days. The slice would push past one milestone and
  detract from the rest of Phase 2.

### C — Repurpose the existing line-overlay infrastructure (nothing exists yet)

- We'd have to build the line-overlay layer first (RHI line topology,
  thick-line geometry shader, screen-space-pixel-thickness shader). That
  was on the deferred list for "selection outline overlay" (M27b). Doing
  it just to back a gizmo inverts the priority.

## Consequences

- Editor module gains an `imguizmo::imguizmo` link dep (PUBLIC because
  EngineState exposes ImGuizmo operation enums to the demo).
- Editor `render()` adds an ImGuizmo call site between chrome and
  `ImGui::Render()`.
- The demo learns to:
  1. Build a model matrix from sphere position (M28a — translate only)
  2. Point `EngineState::active_model` at the live float[16]
  3. After `editor->render()`, decompose the (possibly edited) matrix
     and write back to `sphere.position`
- Future entities (cubes / tube / glTF mesh) follow the same pattern in
  M28b. Anything with a model matrix is naturally manipulable.

## Tradeoffs

- **Build time**: ImGuizmo adds ~1 s compile, ~120 KB to the static lib.
- **Visual consistency**: ImGuizmo respects `ImGuizmo::Style` so we can
  re-skin to the amber accent. Not as tight as a hand-rolled widget
  would be, but close.
- **Vendor lock**: medium. The API surface we depend on is small
  (`Manipulate`, `IsUsing`, `SetRect`, `SetDrawlist`, the OPERATION /
  MODE enums). A future replacement is mostly mechanical.
- **Coordinate system**: ImGuizmo expects column-major matrices in the
  same convention as ImGui-Metal (right-handed). Our `math::Mat4` is
  column-major; an inline conversion at the call site is sufficient.

## Open questions

- Snap step sizes — defer until users complain (1 m translate, 15° rotate
  feels sane).
- Local vs world mode UI — for M28a we hard-pin WORLD; toggle lands when
  it matters (i.e. rotated parent transforms exist).
- Multi-select gizmo — Phase 3, alongside ECS-proper.

## Implementation notes

- `third_party/CMakeLists.txt` adds the FetchContent entry + a
  STATIC target `imguizmo::imguizmo` built against `imgui::imgui`.
- `engine/editor/CMakeLists.txt` adds `imguizmo::imguizmo` to PUBLIC
  link libs.
- `engine/editor/src/editor.cpp` calls
  `ImGuizmo::BeginFrame()` after `ImGui::NewFrame()`,
  `ImGuizmo::SetRect(0, 0, drawable_w, drawable_h)`,
  `ImGuizmo::SetOrthographic(false)`, then
  `ImGuizmo::Manipulate(view, proj, op, mode, model)` if
  `state.active_model != nullptr`.
- `EngineState` gains:
  - `float* active_model;  // 16 floats, row-major; nullable`
  - `const float* view_matrix;  const float* projection_matrix;`
  - The current operation (translate / rotate / scale) lives inside
    `Editor` and is exposed via `Editor::gizmo_op()`.
- W / E / R hotkeys handled in the demo (same place F1 already lives).

## References

- [ADR-0014 — Dear ImGui editor](./0014-imgui-editor.md)
- [ADR-0015 — Input via ImGuiIO](./0015-input-via-imguiio.md) — same
  hover-routing gate used here
- [ImGuizmo repo](https://github.com/CedricGuillemet/ImGuizmo) — README +
  `example/main.cpp` show the canonical integration shape
