# ADR-0015 — Input model: ImGuiIO as the primary input source (Phase 2)

- **Status**: accepted
- **Date**: 2026-05-13
- **Deciders**: renderer / editor
- **Tags**: input, editor, camera, platform

## Context

Phase 1 + 1.5 shipped without any way to move the camera at runtime — the
demo's `Camera` was positioned once at startup and never touched again. Now
that the editor (Phase 2) is mature, the immediate developer-facing need is
viewport navigation: free-fly (FPS-style) and orbit-around-pivot
(Maya-style) so a contributor can actually look around the rendered scene.

The engine already pulls Dear ImGui's full stack (ADR-0014) for the editor
chrome: ImGui's OSX backend monitors NSEvents through `NSEvent
addLocalMonitorForEvents`, populates `ImGuiIO` (`MousePos`, `MouseDelta`,
`MouseWheel`, `MouseDown[]`, `KeyShift`/`KeyCtrl`/`KeyAlt`/`KeySuper`,
`IsKeyDown(ImGuiKey_*)`), and runs the event loop inside
`ImGui_ImplOSX_NewFrame`. That state is sitting there every frame, already
DPI-correct (logical-points coordinate space matching our widget hit-tests).

Two paths to expose input to engine systems:

1. **Use ImGuiIO directly** — call `ImGui::GetIO()` in any place that
   wants input, or surface a translated snapshot on the editor.
2. **Roll our own** input layer that captures NSEvents directly (e.g.
   `Window::poll_events() -> std::span<InputEvent>`), parallel to ImGui's.

## Decision

**Use ImGuiIO as the engine's primary input source while the editor is
attached.** The editor owns translation from ImGuiIO into a small,
engine-shaped `mge::scene::InputState` struct and exposes it via
`Editor::input_state()` along with `Editor::viewport_hovered()`. Camera
controllers and any other interactive system read that snapshot — they
never touch ImGui themselves.

When the editor is *not* attached, no input is captured today. The demo's
camera stays static (matches Phase 1 behaviour). A future milestone (M26b)
adds direct NSEvent capture in the platform layer for editor-off mode.

## Alternatives considered

### A — ImGuiIO via the editor (chosen)

- **Pros**: zero duplication. ImGui already DPI-aware. ImGui resolves
  modifier-key semantics across keyboard layouts. ImGui handles
  `WantCaptureMouse` / `WantCaptureKeyboard` for us — we just need to
  combine its hover signal with our viewport rect (one extra bool).
- **Cons**: input dies when editor is off. ImGuiKey enum doesn't cover
  every NS key; we whitelist what we need.

### B — Parallel NSEvent capture in `Window`

- **Pros**: works without the editor. Owns its own event queue, no
  ImGui dependency.
- **Cons**: duplicate event capture with ImGui's OSX backend (both would
  install `NSEvent` monitors). DPI math + modifier flag translation has
  to be reimplemented and kept in sync. Bigger surface area to keep
  correct as macOS evolves.

### C — Move ImGui's OSX backend out of the editor into the platform layer

- **Pros**: input becomes universal; editor becomes optional.
- **Cons**: leaks an "editor framework" into `engine/platform/`. The
  whole point of the editor module is to be a clean opt-in. This
  inverts the dependency direction.

### D — Hand-rolled NSEvent shim *plus* feed events into ImGui

- **Pros**: technically the cleanest cross-platform path.
- **Cons**: largest implementation cost. Not worth it for an
  Apple-Silicon-first engine pre-Phase-4.

## Consequences

- Camera controllers (`FlyCameraController`, `OrbitCameraController`) take
  `const InputState&` — they are pure functions of input + time and
  trivially unit-testable.
- `Editor` now depends on `mge::scene` (one-way: editor → scene). Scene
  has no editor dep.
- The demo without `--editor` can't move the camera until M26b lands.
  Acceptable trade today (the engine is primarily authored *with* the
  editor up).
- A future second backend (Vulkan / DX12 / iOS) inherits ImGui's
  existing platform shims (`imgui_impl_glfw`, `imgui_impl_sdl`,
  `imgui_impl_win32`) and `InputState` stays unchanged — only the
  populate-from-ImGuiIO function moves.

## Tradeoffs

- **Coupling**: editor module gains a `mge::scene` link dep (~free; both
  are macOS-arm64 static libs).
- **Latency**: 1-frame lag between the user moving the mouse and the
  camera moving on screen, because the editor populates `InputState`
  inside its own FG pass (encoded after the camera has already produced
  its `view_proj` for the same frame). Imperceptible at 60+ fps.
  Removable later by splitting the editor frame into `begin_input` +
  `render` hooks, but the complexity isn't justified for M26a.
- **Coverage**: only the keys we explicitly translate via `imgui_to_key()`
  reach the engine. Adding F-keys / number row / arrows is a one-line
  enum addition + switch case.

## Open questions

- Should `InputState` carry per-frame text input (`io.InputQueueCharacters`)
  for future in-viewport text widgets? Defer until needed.
- Multi-viewport input (M26b ortho cameras) — does each viewport need its
  own `InputState`, or just a `viewport_id` on the shared one?

## Implementation notes

- `engine/scene/include/mge/scene/input_state.h` — pure POD struct
  (mouse, scroll, button edges, key bits, modifier flags, hover hints).
- `engine/scene/include/mge/scene/camera_controller.h` +
  `engine/scene/src/camera_controller.cpp` — `FlyCameraController` (RMB
  + WASD + mouse-look) and `OrbitCameraController` (Maya conventions:
  Alt+LMB orbit, Alt+MMB pan, Alt+RMB/scroll dolly).
- `engine/editor/src/editor.cpp` — `populate_input_state(out, hovered)`
  free function in an anonymous namespace, translates `ImGuiIO` →
  `InputState` once per `Editor::render()`.
- `examples/hello_metal/main.cpp` — holds both controllers; Tab toggles
  the active one; runs `controller.update(camera, in_state, dt)` each
  iteration after `app.poll_events()`.

## References

- [ADR-0014 — Dear ImGui editor](./0014-imgui-editor.md) — establishes
  the editor as the host for ImGui state.
- Dear ImGui's
  [`imgui_impl_osx.mm`](https://github.com/ocornut/imgui/blob/master/backends/imgui_impl_osx.mm)
  — source of `MouseDelta`, key state, modifier flags.
- Maya viewport navigation conventions (Alt+LMB/MMB/RMB) — same in
  Houdini, 3ds Max, Blender (with the "industry-standard keymap"
  preset), Marmoset Toolbag.
