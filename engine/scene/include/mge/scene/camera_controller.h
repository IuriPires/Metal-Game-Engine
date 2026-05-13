#pragma once

// M26a — Two concrete camera controllers: free-fly (FPS-style) and orbit
// (Maya-style). Both consume an `InputState` snapshot and write to a
// `Camera` reference. Sharing a common base class through virtual dispatch
// isn't worth it for two implementations — the demo holds one of each and
// calls the active one's `update()` based on a mode enum.
//
// The controllers are pure logic — no platform / ImGui dependency. The
// caller is responsible for populating `InputState` from whatever event
// source it has (today: ImGuiIO; tomorrow: NSEvent direct).

#include "mge/math/vec.h"
#include "mge/scene/camera.h"
#include "mge/scene/input_state.h"

namespace mge::scene {

// ---------------- Fly (FPS-style) ----------------
//
// Right mouse button hold enables look. While held:
//   - mouse delta yaws (X) and pitches (Y) the view
//   - WASD translates along the camera basis (forward/right)
//   - Q/E or Space/Ctrl translates along world up/down
//   - Shift multiplies speed by `boost`, default 4x
//
// When RMB is not held the controller is inert — keys do nothing, mouse
// motion is ignored. This matches the editor convention (Unity, Unreal,
// Godot all use the same RMB-gated mode so accidental clicks don't fly
// the camera through the level).
class FlyCameraController {
public:
    // Tunables — public so the editor can expose them as cvars later.
    float move_speed = 6.0f;    // m/s
    float boost      = 4.0f;    // Shift multiplier
    float look_sens  = 0.0035f; // radians per logical-point of mouse delta
    float pitch_clamp = 1.4835f;  // ~85° in radians

    // Initialise yaw/pitch from the camera's current orientation so the
    // controller picks up wherever look_at left off, without snapping.
    void sync_from(const Camera& cam) noexcept;

    void update(Camera& cam, const InputState& in, float dt) noexcept;

    [[nodiscard]] float yaw()   const noexcept { return yaw_; }
    [[nodiscard]] float pitch() const noexcept { return pitch_; }

private:
    float yaw_   = 0.0f;  // radians, around world Y
    float pitch_ = 0.0f;  // radians, around camera right
};

// ---------------- Orbit (Maya-style) ----------------
//
// Maya / Houdini / Blender-with-emulation use the Alt modifier (the
// **Option** key on macOS, ⌥; same physical key, just a different label).
// While Option/Alt is held:
//   - LMB + drag           = orbit (yaw + pitch around the pivot)
//   - Shift + LMB + drag   = pan (Mac-friendly alternate; trackpads have
//                                  no MMB so this is the primary path)
//   - MMB + drag           = pan (legacy 3-button-mouse binding, kept
//                                  working for users with external mice)
//   - RMB + drag           = dolly (vertical drag in/out)
//   - scroll wheel         = dolly (works without any modifier; 2-finger
//                                    drag on a Mac trackpad reaches this
//                                    path)
//
// Without Option/Alt the controller respects only the scroll wheel for
// dolly, because scroll is unambiguous and otherwise the user has no way
// to zoom in / out without holding a modifier.
//
// State is { pivot, yaw, pitch, distance }. The camera's eye + look_at are
// derived from those four numbers each frame, so the rest of the engine
// (Camera, FrustumExtractor, etc.) doesn't need to know orbit ever existed.
class OrbitCameraController {
public:
    math::Vec3 pivot{0.0f, 1.2f, 0.0f};
    float yaw       = 0.0f;       // radians, around world Y
    float pitch     = 0.3f;       // radians, positive looks down
    float distance  = 7.0f;       // m
    float min_distance = 0.5f;
    float max_distance = 200.0f;

    float orbit_sens = 0.006f;    // rad / logical-point
    float pan_sens   = 0.0025f;   // world-units / logical-point at distance=1
    float dolly_sens = 0.05f;     // fraction-of-distance / wheel-notch
    float drag_dolly_sens = 0.01f;// fraction-of-distance / logical-point (RMB drag)
    float pitch_clamp = 1.4835f;  // ~85° to avoid gimbal flip

    // Initialise pivot/yaw/pitch/distance from the camera's current look_at
    // so the controller picks up smoothly without snapping.
    void sync_from(const Camera& cam) noexcept;

    void update(Camera& cam, const InputState& in, float dt) noexcept;

    // Re-derive eye + view matrix from the current pivot/yaw/pitch/distance
    // without consulting the input state. The demo calls this on init and
    // after `sync_from()` so the first rendered frame already shows the
    // controller's pose.
    void apply(Camera& cam) const noexcept;
};

}  // namespace mge::scene
