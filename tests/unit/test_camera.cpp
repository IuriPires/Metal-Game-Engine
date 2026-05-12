#include "mge/scene/camera.h"

#include <doctest/doctest.h>

#include <cmath>

using mge::scene::Camera;
using namespace mge::math;

TEST_CASE("Camera default view is identity-ish for eye on +Z") {
    Camera c;
    c.look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0});
    const Mat4& v = c.view();
    // Point at origin should land at (0, 0, -3) in view space (camera looks down -Z).
    const Vec4 p = v * Vec4{0, 0, 0, 1};
    CHECK(p.x == doctest::Approx(0.0));
    CHECK(p.y == doctest::Approx(0.0));
    CHECK(p.z == doctest::Approx(-3.0));
}

TEST_CASE("Camera projection maps the origin point inside NDC") {
    Camera c;
    c.set_perspective(radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    c.look_at({0, 0, 5}, {0, 0, 0}, {0, 1, 0});

    const Vec4 clip = c.view_projection() * Vec4{0, 0, 0, 1};
    // origin is in front of camera; clip.w > 0
    REQUIRE(clip.w > 0.0f);
    const float ndc_x = clip.x / clip.w;
    const float ndc_y = clip.y / clip.w;
    const float ndc_z = clip.z / clip.w;
    CHECK(std::abs(ndc_x) < 1.0f);
    CHECK(std::abs(ndc_y) < 1.0f);
    CHECK(ndc_z >= 0.0f);
    CHECK(ndc_z <= 1.0f);
}

TEST_CASE("Camera caches projection until aspect changes") {
    Camera c;
    c.set_perspective(radians(45.0f), 1.0f, 0.1f, 50.0f);
    const Mat4 p1 = c.projection();
    c.set_aspect(1.0f);  // same value, no rebuild
    const Mat4 p2 = c.projection();
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t j = 0; j < 4; ++j) {
            CHECK(p1[i][j] == doctest::Approx(p2[i][j]));
        }
    }
    c.set_aspect(2.0f);
    const Mat4 p3 = c.projection();
    // x scale changes when aspect changes
    CHECK(p3[0][0] != doctest::Approx(p1[0][0]));
}
