#include "mge/assets/pbr_mesh.h"

#include <doctest/doctest.h>

#include <cmath>

using mge::assets::make_cube_pbr;
using mge::assets::make_sphere_pbr;
using mge::assets::PbrVertex;

TEST_CASE("make_sphere_pbr default counts") {
    const auto m = make_sphere_pbr();  // 16 lat segs x 32 lon segs default
    // Vertices: (lat_segs + 1) * (lon_segs + 1) = 17 * 33 = 561
    CHECK(m.vertices.size() == 17 * 33);
    // Indices: lat_segs * lon_segs * 6 = 16 * 32 * 6 = 3072
    CHECK(m.indices.size() == 16 * 32 * 6);
}

TEST_CASE("make_sphere_pbr vertices lie on the unit sphere") {
    const auto m = make_sphere_pbr(8, 16);
    for (const auto& v : m.vertices) {
        const float r2 = v.position.x * v.position.x +
                         v.position.y * v.position.y +
                         v.position.z * v.position.z;
        CHECK(std::abs(r2 - 1.0f) < 1e-5f);
    }
}

TEST_CASE("make_sphere_pbr normal equals position for a unit sphere") {
    const auto m = make_sphere_pbr(8, 16);
    for (const auto& v : m.vertices) {
        CHECK(v.normal.x == doctest::Approx(v.position.x));
        CHECK(v.normal.y == doctest::Approx(v.position.y));
        CHECK(v.normal.z == doctest::Approx(v.position.z));
    }
}

TEST_CASE("make_sphere_pbr indices are in range") {
    const auto m = make_sphere_pbr(6, 8);
    for (auto i : m.indices) {
        CHECK(i < m.vertices.size());
    }
}

TEST_CASE("make_cube_pbr has 24 vertices and 36 indices") {
    const auto m = make_cube_pbr();
    CHECK(m.vertices.size() == 24);
    CHECK(m.indices.size() == 36);
}
