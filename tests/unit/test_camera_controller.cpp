#include "mge/scene/camera_controller.h"

#include <doctest/doctest.h>

#include <cmath>

using mge::scene::Button;
using mge::scene::Camera;
using mge::scene::FlyCameraController;
using mge::scene::InputState;
using mge::scene::Key;
using mge::scene::OrbitCameraController;
using namespace mge::math;

namespace {

[[nodiscard]] InputState make_input() {
    InputState in;
    in.viewport_hovered = true;
    return in;
}

constexpr float k_eps = 1e-4f;

}  // namespace

// ============== Fly ==============

TEST_CASE("FlyCameraController is inert when RMB is not held") {
    Camera c;
    c.look_at({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    FlyCameraController fly;
    fly.sync_from(c);

    InputState in = make_input();
    in.key_down[static_cast<std::size_t>(Key::W)] = true;
    in.mouse_delta_x = 100.0f;
    in.mouse_delta_y = 50.0f;

    const Vec3 eye_before = c.eye();
    fly.update(c, in, 1.0f / 60.0f);

    CHECK(c.eye().x == doctest::Approx(eye_before.x));
    CHECK(c.eye().y == doctest::Approx(eye_before.y));
    CHECK(c.eye().z == doctest::Approx(eye_before.z));
}

TEST_CASE("FlyCameraController moves forward along view when W is held with RMB") {
    Camera c;
    c.look_at({0, 0, 5}, {0, 0, 0}, {0, 1, 0});  // looking down -Z
    FlyCameraController fly;
    fly.sync_from(c);

    InputState in = make_input();
    in.button_down[static_cast<std::size_t>(Button::Right)] = true;
    in.key_down[static_cast<std::size_t>(Key::W)] = true;

    const float dt    = 0.5f;
    const float speed = fly.move_speed;
    fly.update(c, in, dt);

    // 0.5 s * 6 m/s = 3 m forward → eye moves from z=5 toward z=2.
    CHECK(c.eye().z == doctest::Approx(5.0f - speed * dt).epsilon(0.01));
    CHECK(c.eye().x == doctest::Approx(0.0f).epsilon(0.01));
}

TEST_CASE("FlyCameraController shift boosts move speed") {
    Camera c;
    c.look_at({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    FlyCameraController fly;
    fly.sync_from(c);

    InputState in = make_input();
    in.button_down[static_cast<std::size_t>(Button::Right)] = true;
    in.key_down[static_cast<std::size_t>(Key::W)] = true;
    in.mods.shift = true;

    const float dt = 0.5f;
    fly.update(c, in, dt);

    const float expected = fly.move_speed * fly.boost * dt;
    CHECK(c.eye().z == doctest::Approx(5.0f - expected).epsilon(0.01));
}

TEST_CASE("FlyCameraController pitch is clamped near ±90°") {
    Camera c;
    c.look_at({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    FlyCameraController fly;
    fly.sync_from(c);

    InputState in = make_input();
    in.button_down[static_cast<std::size_t>(Button::Right)] = true;
    // Huge downward drag — would otherwise pitch past 90°.
    in.mouse_delta_y = 100000.0f;
    fly.update(c, in, 1.0f / 60.0f);

    CHECK(fly.pitch() <= fly.pitch_clamp + k_eps);
    CHECK(fly.pitch() >= -fly.pitch_clamp - k_eps);
}

// ============== Orbit ==============

TEST_CASE("OrbitCameraController sync_from preserves the eye position") {
    Camera c;
    c.look_at({3.0f, 4.0f, 5.0f}, {0, 0, 0}, {0, 1, 0});

    OrbitCameraController orb;
    orb.sync_from(c);
    orb.apply(c);

    CHECK(c.eye().x == doctest::Approx(3.0f).epsilon(1e-4));
    CHECK(c.eye().y == doctest::Approx(4.0f).epsilon(1e-4));
    CHECK(c.eye().z == doctest::Approx(5.0f).epsilon(1e-4));
    CHECK(c.center().x == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(c.center().y == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(c.center().z == doctest::Approx(0.0f).epsilon(1e-4));
}

TEST_CASE("OrbitCameraController orbits when Alt+LMB is dragged") {
    Camera c;
    c.look_at({0.0f, 0.0f, 5.0f}, {0, 0, 0}, {0, 1, 0});
    OrbitCameraController orb;
    orb.sync_from(c);
    const float yaw_before = orb.yaw;

    InputState in = make_input();
    in.mods.alt = true;
    in.button_down[static_cast<std::size_t>(Button::Left)] = true;
    in.mouse_delta_x = 100.0f;

    orb.update(c, in, 1.0f / 60.0f);

    CHECK(orb.yaw != yaw_before);
    // distance preserved.
    CHECK(orb.distance == doctest::Approx(5.0f).epsilon(1e-4));
}

TEST_CASE("OrbitCameraController dollies on scroll wheel without modifiers") {
    Camera c;
    c.look_at({0.0f, 0.0f, 5.0f}, {0, 0, 0}, {0, 1, 0});
    OrbitCameraController orb;
    orb.sync_from(c);
    const float d_before = orb.distance;

    InputState in = make_input();
    in.scroll_y = 1.0f;   // wheel forward → dolly in (smaller distance)
    orb.update(c, in, 1.0f / 60.0f);
    CHECK(orb.distance < d_before);

    in.scroll_y = -2.0f;  // wheel back → dolly out
    orb.update(c, in, 1.0f / 60.0f);
    CHECK(orb.distance > d_before);
}

TEST_CASE("OrbitCameraController distance is clamped to [min, max]") {
    Camera c;
    c.look_at({0.0f, 0.0f, 5.0f}, {0, 0, 0}, {0, 1, 0});
    OrbitCameraController orb;
    orb.sync_from(c);

    InputState in = make_input();
    in.scroll_y = 10000.0f;
    orb.update(c, in, 1.0f / 60.0f);
    CHECK(orb.distance >= orb.min_distance - k_eps);

    in.scroll_y = -10000.0f;
    orb.update(c, in, 1.0f / 60.0f);
    CHECK(orb.distance <= orb.max_distance + k_eps);
}

TEST_CASE("OrbitCameraController ignores input when viewport is not hovered") {
    Camera c;
    c.look_at({0.0f, 0.0f, 5.0f}, {0, 0, 0}, {0, 1, 0});
    OrbitCameraController orb;
    orb.sync_from(c);
    const float yaw_before      = orb.yaw;
    const float distance_before = orb.distance;

    InputState in = make_input();
    in.viewport_hovered = false;
    in.mods.alt = true;
    in.button_down[static_cast<std::size_t>(Button::Left)] = true;
    in.mouse_delta_x = 200.0f;
    in.scroll_y = 5.0f;
    orb.update(c, in, 1.0f / 60.0f);

    CHECK(orb.yaw == doctest::Approx(yaw_before));
    CHECK(orb.distance == doctest::Approx(distance_before));
}

TEST_CASE("OrbitCameraController pan moves pivot in the screen plane") {
    Camera c;
    c.look_at({0.0f, 0.0f, 5.0f}, {0, 0, 0}, {0, 1, 0});
    OrbitCameraController orb;
    orb.sync_from(c);
    const Vec3 pivot_before = orb.pivot;

    InputState in = make_input();
    in.mods.alt = true;
    in.button_down[static_cast<std::size_t>(Button::Middle)] = true;
    in.mouse_delta_x = 100.0f;
    orb.update(c, in, 1.0f / 60.0f);

    // Pan should move the pivot horizontally (in camera-right basis).
    CHECK(orb.pivot.x != pivot_before.x);
    // The pivot's Y stays put when only horizontal drag is applied.
    CHECK(orb.pivot.y == doctest::Approx(pivot_before.y));
}
