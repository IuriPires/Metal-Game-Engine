#include "mge/scene/camera_controller.h"

#include "mge/math/mat.h"
#include "mge/math/vec.h"

#include <algorithm>
#include <cmath>

namespace mge::scene {

namespace {

// Convert yaw + pitch (both in radians, yaw around +Y, pitch around camera
// right) into a unit forward vector in a right-handed world with +Y up.
[[nodiscard]] math::Vec3 yaw_pitch_to_forward(float yaw, float pitch) noexcept {
    const float cp = std::cos(pitch);
    return math::normalize(math::Vec3{
        std::sin(yaw) * cp,
        std::sin(pitch),
        -std::cos(yaw) * cp,
    });
}

}  // namespace

// ===================== FlyCameraController =====================

void FlyCameraController::sync_from(const Camera& cam) noexcept {
    const math::Vec3 fwd = cam.forward();
    pitch_ = std::asin(std::clamp(fwd.y, -1.0f, 1.0f));
    // yaw such that yaw_pitch_to_forward(yaw, pitch).xz matches fwd.xz.
    // fwd.x =  sin(yaw)*cos(pitch); fwd.z = -cos(yaw)*cos(pitch)
    // => yaw = atan2(fwd.x, -fwd.z)
    yaw_ = std::atan2(fwd.x, -fwd.z);
}

void FlyCameraController::update(Camera& cam, const InputState& in, float dt) noexcept {
    if (dt <= 0.0f) return;

    const bool rmb = in.viewport_hovered && in.is_down(Button::Right);
    if (rmb) {
        yaw_   -= in.mouse_delta_x * look_sens;
        pitch_ -= in.mouse_delta_y * look_sens;
        pitch_  = std::clamp(pitch_, -pitch_clamp, pitch_clamp);
    }

    const math::Vec3 fwd = yaw_pitch_to_forward(yaw_, pitch_);
    const math::Vec3 world_up{0.0f, 1.0f, 0.0f};
    const math::Vec3 right = math::normalize(math::cross(fwd, world_up));
    // The camera's up vector should stay world-up so roll doesn't drift; the
    // view matrix uses `look_at_rh(eye, eye+fwd, world_up)` semantics.

    math::Vec3 move{0.0f, 0.0f, 0.0f};
    if (rmb && !in.keyboard_focused) {
        if (in.is_down(Key::W)) move = move + fwd;
        if (in.is_down(Key::S)) move = move - fwd;
        if (in.is_down(Key::D)) move = move + right;
        if (in.is_down(Key::A)) move = move - right;
        if (in.is_down(Key::E) || in.is_down(Key::Space)) move = move + world_up;
        if (in.is_down(Key::Q)) move = move - world_up;
    }

    const float speed = move_speed * (in.mods.shift ? boost : 1.0f);
    const float m_len = math::length(move);
    if (m_len > 1e-6f) {
        move = move * (1.0f / m_len);
        const math::Vec3 eye = cam.eye() + move * (speed * dt);
        cam.look_at(eye, eye + fwd, world_up);
    } else if (rmb) {
        // Update look direction even when not moving so the rotation
        // applies immediately.
        const math::Vec3 eye = cam.eye();
        cam.look_at(eye, eye + fwd, world_up);
    }
}

// ===================== OrbitCameraController =====================

void OrbitCameraController::sync_from(const Camera& cam) noexcept {
    const math::Vec3 eye    = cam.eye();
    const math::Vec3 center = cam.center();
    const math::Vec3 to_eye = eye - center;
    distance = std::max(min_distance, math::length(to_eye));
    pivot    = center;

    // Derive yaw/pitch from `to_eye` (the offset from pivot to eye).
    // to_eye = distance * (cos(pitch)*sin(yaw), sin(pitch), -cos(pitch)*cos(yaw))
    pitch = std::asin(std::clamp(to_eye.y / distance, -1.0f, 1.0f));
    yaw   = std::atan2(to_eye.x, -to_eye.z);
}

void OrbitCameraController::update(Camera& cam, const InputState& in, float dt) noexcept {
    (void)dt;  // orbit motion is mouse-driven, not time-integrated.
    const bool hover = in.viewport_hovered;

    // Mac trackpads / Magic Mouse don't have an MMB, so the "Option + MMB
    // pan" Maya convention is unreachable without an external 3-button
    // mouse. We therefore accept Option + Shift + LMB as an alternate pan
    // binding — natural on a trackpad, and harmless on a regular mouse
    // (Shift isn't otherwise used by the orbit controller). Option + MMB
    // still works for users on a 3-button mouse.
    if (hover && in.mods.alt) {
        const bool shift_pan = in.mods.shift && in.is_down(Button::Left);
        const bool mmb_pan   = in.is_down(Button::Middle);
        if (in.is_down(Button::Left) && !in.mods.shift) {
            yaw   -= in.mouse_delta_x * orbit_sens;
            pitch += in.mouse_delta_y * orbit_sens;
            pitch  = std::clamp(pitch, -pitch_clamp, pitch_clamp);
        }
        if (shift_pan || mmb_pan) {
            // Pan: move pivot along the camera basis at a rate proportional
            // to the orbit distance so the visual speed stays consistent at
            // any zoom level.
            const math::Vec3 fwd = yaw_pitch_to_forward(yaw, pitch);
            const math::Vec3 world_up{0.0f, 1.0f, 0.0f};
            const math::Vec3 right = math::normalize(math::cross(fwd, world_up));
            const math::Vec3 up    = math::cross(right, fwd);
            const float k = pan_sens * distance;
            pivot = pivot - right * (in.mouse_delta_x * k)
                          + up    * (in.mouse_delta_y * k);
        }
        if (in.is_down(Button::Right)) {
            // RMB drag dolly — vertical drag = dolly in/out.
            const float f = 1.0f + in.mouse_delta_y * drag_dolly_sens;
            distance = std::clamp(distance * f, min_distance, max_distance);
        }
    }

    // Scroll wheel always dollies, modifiers or not — feels natural and
    // disambiguates "I want to zoom" without forcing a modifier press.
    if (hover && std::abs(in.scroll_y) > 0.0f) {
        const float f = 1.0f - in.scroll_y * dolly_sens;
        distance = std::clamp(distance * f, min_distance, max_distance);
    }

    apply(cam);
}

void OrbitCameraController::apply(Camera& cam) const noexcept {
    const math::Vec3 offset = yaw_pitch_to_forward(yaw, pitch) * distance;
    const math::Vec3 eye{pivot.x + offset.x, pivot.y + offset.y, pivot.z + offset.z};
    cam.look_at(eye, pivot, math::Vec3{0.0f, 1.0f, 0.0f});
}

}  // namespace mge::scene
