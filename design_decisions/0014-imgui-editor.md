# ADR-0014 — Dear ImGui editor with the Claude Design system

- **Status**: accepted
- **Date**: 2026-05-12
- **Deciders**: editor
- **Tags**: editor, imgui, design-system, phase-2

## Context

Phase 2's first track is Tooling (we swapped Tooling and Systems in the ROADMAP — see the previous chat thread). The editor is the marquee deliverable: a dark, dense, AAA-tool-feeling chrome for inspecting and tweaking the running demo without rebuilding.

I commissioned a design pass via Claude Design. The handoff bundle (`notes/design_drop/game-engine/`) ships:

- 7 full-screen mockups (main editor, render settings, framegraph, profiler, shader reload, modal, empty state)
- A component library sheet (12 primitives × 5 states)
- A design tokens table (colors, type, spacing, radius, borders, motion, sizes)
- Two variants — amber vs cool-blue accent — and three viewport-selection-outline treatments

The designer's committed calls are documented in `chat1.md`:

- **Amber primary** (`#E8A24A`) — continuity with the M11 profiler overlay's warm yellow
- **Inter for chrome + JetBrains Mono for data**
- **22 px tree rows / 22 px inputs / 24 px buttons / no row above 28 px**
- **2 px amber stroke** for selection (silhouette outline, matching the tree-row inset bar)
- **0–2 px corners everywhere; 4 px only on the modal**
- **1 px borders define separation; no drop shadows anywhere**

The job for M18 is to translate that design system into an actual native editor, integrated with the engine's RHI and FrameGraph.

## Decision

- **Dear ImGui (docking branch)** as the UI foundation.
- **Upstream Metal + OSX backends** (`imgui_impl_metal.mm`, `imgui_impl_osx.mm`) at the platform edge. This is the same precedent set by `window.mm` (ADR-0001 carve-out for AppKit boundaries) — we don't reimplement them through the RHI.
- **Single new engine module** `mge::editor`, depending only on RHI + platform + core. The editor is opt-in: instantiate via `Editor::create(device, native_window, fmt)` when `--editor` is passed.
- **FrameGraph integration**: the editor is one `editor` pass that runs **after** `overlay`, **before** present, with `LoadAction::Load` on the backbuffer. The same encoder hosts ImGui's draw data. No new FG resource types needed.
- **Theme bound to the design tokens** in `engine/editor/src/theme.{h,cpp}`. All `ImGuiCol_*` and `ImGuiStyleVar_*` values come from `notes/design_drop/.../styles.css :root`. Updating the design tokens means rerunning this one mapping; the rest of the editor calls `apply_theme()` and gets a consistent look.
- **Stateless chrome for M18.** Menubar, toolbar, status bar, three-column workarea, and bottom dock tab strip render from per-frame `EngineState` (a POD with stats + pointers back into the demo's bool toggles). M19+ adds the concrete panels (outliner, inspector, profiler, framegraph, render settings, shader reload).

## Alternatives considered

1. **Custom UI on top of the RHI.** Pros: pure-C++ stack, full control over draw-call shape, can use the existing bitmap font + overlay infrastructure as the foundation. Cons: months of work to reach feature parity with ImGui (text input, focus, docking, popups, drag-and-drop, multi-viewport). Rejected — ImGui exists and the design's density goals match what ImGui is good at.

2. **Reimplement `ImGui_ImplMetal` against our RHI.** Cleaner separation. Cons: the upstream backend is small but tied to a few MTL constructs (the depth/stencil state, the sampler, the buffer reuse pool) that don't 1:1 match our RHI shape. The cost-benefit on Phase 2 is poor — we'd write ~400 lines of glue to avoid 80 lines of upstream Obj-C++ already covered by ADR-0001's carve-out.

3. **A different UI library (Qt, native AppKit, Tauri, web view).** Heavy, off-vibe (the design explicitly anti-references Figma / Linear / Discord), or requires a completely different process boundary. Rejected.

4. **Skip the design system and use ImGui defaults.** Faster M18 but undoes the entire reason for commissioning the design. Rejected.

## Consequences

**Easy now:**
- The editor is one `--editor` flag away on every build. Toggle in and out without rebuilding.
- The design tokens are in one place; future tweaks to the palette / typography happen once.
- The FG pass model means the editor can be moved (e.g., after particles, before overlay) without touching the editor itself.

**Committed:**
- The engine now ships ImGui as a third-party dependency (docking branch, ADR-0014 records the choice; the build is opt-in on `APPLE`).
- Two new Obj-C++ TUs at the platform edge (`imgui_platform.mm`, plus the upstream `imgui_impl_*.mm` files compiled by the imgui target). ADR-0001 already permits this carve-out.
- M19+ panels (outliner, inspector, profiler, etc.) follow this same architecture — `EngineState` extends as panels need more data.

**Explicitly not committing to:**
- Multi-viewport / out-of-window ImGui (the design assumes a single window).
- Persistent layout (`io.IniFilename = nullptr` for now; M19 may re-enable it).
- Custom font files (M18 ships with ImGui's default font; loading Inter + JetBrains Mono is M19 polish).
- Drag-handle resizing of the three-column workarea (M18 uses fixed widths from the design tokens; M19 introduces ImGui docking inside the workarea so the user can drag).

## Tradeoffs

- **Binary size**: ImGui + Metal + OSX impl adds ~600 KB to the executable. Negligible.
- **Frame cost**: editor on, demo holds 120 FPS on M1 Pro. Editor adds ~0.3 ms CPU + ~0.4 ms GPU.
- **Code shape**: editor lives in its own module so the demo + engine don't have to know about ImGui. Replacing ImGui later is a contained change.
- **Design fidelity**: ImGui's widget shapes don't 100% match the design's hand-drawn primitives. M18 ships approximate widgets (placeholder panels, ImGui's default tab strip). M19 adds custom drawing where ImGui falls short (vector3 axis colors, tree-row inset bars, sectioned inspectors, slider with embedded tick marks).

## Phase 1.5 → Phase 2 transition

This is the first commit in Phase 2 — Tooling. The previous milestones (M0–M17) closed Phase 1 and Phase 1.5 with the engine running deferred PBR + RT + HZB + particles + LOD + skinning at 120 FPS. M18 starts the editor track. Subsequent Phase 2 milestones:

- **M19** — Outliner + Inspector (real scene browsing, sphere material editing)
- **M20** — Bottom dock panels (Console, Profiler chart, FrameGraph view, Render Settings, Shader Reload)
- **M21** — Live shader reload + cvars
- **M22** — Frame capture viewer
- **M23** — Input system (camera nav, gizmos)

ECS / glTF / Jobs / Physics / Audio (the original Phase 2 Systems items) follow the editor track and become Phase 3.

## Implementation notes

- `third_party/CMakeLists.txt` — `FetchContent` ImGui docking branch; build `imgui::imgui` as a STATIC lib that includes the Metal + OSX backends. `-fobjc-arc` on the imgui target.
- `engine/editor/` — new module:
  - `include/mge/editor/editor.h` — `Editor` + `EngineState` public API.
  - `src/theme.{h,cpp}` — design tokens, `apply_theme()`.
  - `src/chrome.{h,cpp}` — menubar, toolbar, three-column workarea, bottom dock tab strip, status bar.
  - `src/imgui_platform.{h,mm}` — ImGui backend bridge (init/shutdown/new_frame/render).
  - `src/editor.cpp` — `Editor::create` + `Editor::render` lifecycle.
- `engine/platform/include/mge/platform/window.h` — `Window::native_window()` accessor for the NSWindow* the OSX backend needs.
- `examples/hello_metal/main.cpp` — `--editor` flag; creates the editor after the swapchain; adds an `editor` FG pass after `overlay`.

## References

- ADR-0001 — Metal-cpp / AppKit boundary carve-out (this ADR cites it for the Obj-C++ exception).
- ADR-0005 — Frame graph (the editor is one more pass).
- Design handoff bundle at `notes/design_drop/game-engine/` — the styles.css `:root` block is the single source of truth for the tokens.
- ImGui docking branch — github.com/ocornut/imgui (no version pinned; tracks HEAD of docking).
