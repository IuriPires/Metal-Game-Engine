#include "mge/math/aabb.h"
#include "mge/math/frustum.h"
#include "mge/math/mat.h"

#include <doctest/doctest.h>

using namespace mge::math;

TEST_CASE("Frustum extracts planes from a perspective view-projection") {
    const Mat4 view = look_at_rh({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    const Mat4 proj = perspective_rh_zo(radians(60.0f), 1.0f, 0.1f, 100.0f);
    const Mat4 vp   = proj * view;
    const Frustum f = Frustum::from_view_projection(vp);

    // Plane normals should be unit-length after normalization.
    for (std::size_t i = 0; i < Frustum::PlaneCount; ++i) {
        const auto& p = f.planes[i];
        const float l = std::sqrt(p.normal.x * p.normal.x + p.normal.y * p.normal.y +
                                   p.normal.z * p.normal.z);
        CHECK(std::abs(l - 1.0f) < 1e-4f);
    }
}

TEST_CASE("aabb_visible accepts an AABB at the camera focus") {
    const Mat4 vp = perspective_rh_zo(radians(60.0f), 1.0f, 0.1f, 100.0f) *
                    look_at_rh({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    const Frustum f = Frustum::from_view_projection(vp);

    const Aabb box = Aabb::from_points(Vec3{-0.5f, -0.5f, -0.5f}, Vec3{0.5f, 0.5f, 0.5f});
    CHECK(aabb_visible(f, box));
}

TEST_CASE("aabb_visible rejects AABBs behind the camera") {
    const Mat4 vp = perspective_rh_zo(radians(60.0f), 1.0f, 0.1f, 100.0f) *
                    look_at_rh({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    const Frustum f = Frustum::from_view_projection(vp);

    // Behind camera (camera is at z=5 looking at origin = -Z), so z > 5 is behind.
    const Aabb behind = Aabb::from_points(Vec3{-0.5f, -0.5f, 10.0f}, Vec3{0.5f, 0.5f, 11.0f});
    CHECK_FALSE(aabb_visible(f, behind));
}

TEST_CASE("aabb_visible rejects AABBs beyond the far plane") {
    const Mat4 vp = perspective_rh_zo(radians(60.0f), 1.0f, 0.1f, 50.0f) *
                    look_at_rh({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    const Frustum f = Frustum::from_view_projection(vp);

    // Far past the far plane.
    const Aabb far_box = Aabb::from_points(Vec3{-0.5f, -0.5f, -200.0f},
                                            Vec3{0.5f, 0.5f, -199.0f});
    CHECK_FALSE(aabb_visible(f, far_box));
}

TEST_CASE("test_aabb returns Inside for fully contained boxes") {
    const Mat4 vp = perspective_rh_zo(radians(90.0f), 1.0f, 0.1f, 100.0f) *
                    look_at_rh({0, 0, 10}, {0, 0, 0}, {0, 1, 0});
    const Frustum f = Frustum::from_view_projection(vp);

    // Small box near origin, well inside.
    const Aabb tiny = Aabb::from_points(Vec3{-0.1f, -0.1f, -0.1f}, Vec3{0.1f, 0.1f, 0.1f});
    CHECK(test_aabb(f, tiny) == CullResult::Inside);
}

TEST_CASE("test_aabb returns Intersecting for boxes that straddle a plane") {
    const Mat4 vp = perspective_rh_zo(radians(60.0f), 1.0f, 0.1f, 100.0f) *
                    look_at_rh({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    const Frustum f = Frustum::from_view_projection(vp);

    // Box that extends beyond the near plane (straddles it).
    const Aabb straddle = Aabb::from_points(Vec3{-0.1f, -0.1f, 4.5f}, Vec3{0.1f, 0.1f, 6.0f});
    const auto r        = test_aabb(f, straddle);
    CHECK(r == CullResult::Intersecting);
}

#if MGE_ARCH_NEON
TEST_CASE("NEON 4-wide cull matches scalar results") {
    const Mat4 vp = perspective_rh_zo(radians(60.0f), 1.6f, 0.1f, 100.0f) *
                    look_at_rh({0, 1, 6}, {0, 0, 0}, {0, 1, 0});
    const Frustum f = Frustum::from_view_projection(vp);

    const Aabb boxes[4] = {
        Aabb::from_points(Vec3{-0.5f, -0.5f, -0.5f}, Vec3{0.5f, 0.5f, 0.5f}),     // visible
        Aabb::from_points(Vec3{-200, -200, -200},    Vec3{-198, -198, -198}),    // outside
        Aabb::from_points(Vec3{2.0f, 2.0f, -2.0f},   Vec3{3.0f, 3.0f, -1.0f}),    // visible-ish
        Aabb::from_points(Vec3{0.0f, 0.0f, 50.0f},   Vec3{1.0f, 1.0f, 51.0f}),    // behind camera
    };

    float minx[4], miny[4], minz[4], maxx[4], maxy[4], maxz[4];
    std::uint32_t neon_visible[4];
    for (int i = 0; i < 4; ++i) {
        minx[i] = boxes[i].min.x; miny[i] = boxes[i].min.y; minz[i] = boxes[i].min.z;
        maxx[i] = boxes[i].max.x; maxy[i] = boxes[i].max.y; maxz[i] = boxes[i].max.z;
    }
    aabb_visible_x4_neon(f, minx, miny, minz, maxx, maxy, maxz, neon_visible);

    for (int i = 0; i < 4; ++i) {
        const bool scalar = aabb_visible(f, boxes[i]);
        CHECK(static_cast<bool>(neon_visible[i]) == scalar);
    }
}
#endif
