#include "mge/math/aabb.h"
#include "mge/scene/camera.h"
#include "mge/scene/picker.h"

#include <doctest/doctest.h>

using namespace mge::math;
using mge::scene::Camera;
using mge::scene::Pickable;
using mge::scene::closest_hit;

// =============== ray_aabb_intersect ===============

TEST_CASE("ray_aabb_intersect hits a unit box straight on") {
    Aabb box = Aabb::from_points({-1, -1, -1}, {1, 1, 1});
    Ray r;
    r.origin = Vec3{0, 0, 5};
    r.dir    = Vec3{0, 0, -1};
    const auto h = ray_aabb_intersect(r, box);
    REQUIRE(h.hit);
    CHECK(h.t_near == doctest::Approx(4.0f));
}

TEST_CASE("ray_aabb_intersect misses when ray points away") {
    Aabb box = Aabb::from_points({-1, -1, -1}, {1, 1, 1});
    Ray r;
    r.origin = Vec3{0, 0, 5};
    r.dir    = Vec3{0, 0, 1};   // pointing away
    CHECK_FALSE(ray_aabb_intersect(r, box).hit);
}

TEST_CASE("ray_aabb_intersect: ray origin inside box returns t_near=0") {
    Aabb box = Aabb::from_points({-1, -1, -1}, {1, 1, 1});
    Ray r;
    r.origin = Vec3{0, 0, 0};
    r.dir    = Vec3{1, 0, 0};
    const auto h = ray_aabb_intersect(r, box);
    REQUIRE(h.hit);
    CHECK(h.t_near == doctest::Approx(0.0f));
}

TEST_CASE("ray_aabb_intersect: parallel ray outside slab misses") {
    Aabb box = Aabb::from_points({-1, -1, -1}, {1, 1, 1});
    Ray r;
    r.origin = Vec3{0, 5, 0};  // above the box on Y
    r.dir    = Vec3{0, 0, -1}; // parallel to Y slab
    CHECK_FALSE(ray_aabb_intersect(r, box).hit);
}

TEST_CASE("ray_aabb_intersect: oblique hit on the corner") {
    Aabb box = Aabb::from_points({0, 0, 0}, {1, 1, 1});
    Ray r;
    r.origin = Vec3{-1, -1, -1};
    r.dir    = normalize(Vec3{1, 1, 1});
    CHECK(ray_aabb_intersect(r, box).hit);
}

// =============== Picker ===============

TEST_CASE("closest_hit returns nullopt when no AABBs are hit") {
    Pickable list[] = {
        {Aabb::from_points({-1, -1, -1}, {1, 1, 1}), 0, 0},
    };
    Ray r;
    r.origin = Vec3{0, 0, 5};
    r.dir    = Vec3{1, 0, 0};  // misses
    CHECK_FALSE(closest_hit(r, list).has_value());
}

TEST_CASE("closest_hit picks the nearest pickable") {
    Pickable list[] = {
        {Aabb::from_points({-1, -1, -3}, {1, 1, -1}),  1, 100},  // closer
        {Aabb::from_points({-1, -1, -8}, {1, 1, -6}),  2, 200},  // farther
    };
    Ray r;
    r.origin = Vec3{0, 0, 5};
    r.dir    = Vec3{0, 0, -1};
    const auto h = closest_hit(r, list);
    REQUIRE(h.has_value());
    CHECK(h->pickable_index == 0);
    CHECK(list[h->pickable_index].tag   == 1);
    CHECK(list[h->pickable_index].index == 100);
}

// =============== Camera::ray_from_ndc ===============

TEST_CASE("Camera::ray_from_ndc center NDC = view forward") {
    Camera c;
    c.set_perspective(radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    c.look_at({0, 0, 5}, {0, 0, 0}, {0, 1, 0});  // looking down -Z

    const auto r = c.ray_from_ndc(0.0f, 0.0f);
    CHECK(r.origin.x == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(r.origin.y == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(r.origin.z == doctest::Approx(5.0f).epsilon(1e-4));
    // Direction should be (0, 0, -1) — straight ahead.
    CHECK(r.dir.x == doctest::Approx(0.0f).epsilon(1e-3));
    CHECK(r.dir.y == doctest::Approx(0.0f).epsilon(1e-3));
    CHECK(r.dir.z == doctest::Approx(-1.0f).epsilon(1e-3));
}

TEST_CASE("Camera::ray_from_ndc center hits a centered AABB") {
    Camera c;
    c.set_perspective(radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    c.look_at({0, 0, 5}, {0, 0, 0}, {0, 1, 0});

    Aabb box = Aabb::from_points({-1, -1, -1}, {1, 1, 1});
    const auto r = c.ray_from_ndc(0.0f, 0.0f);
    CHECK(ray_aabb_intersect(r, box).hit);
}
