#include "mge/assets/primitives.h"

#include <doctest/doctest.h>

#include <cmath>

using mge::assets::LambertVertex;
using mge::assets::make_cube;

TEST_CASE("make_cube has 24 unique vertices and 36 indices") {
    const auto m = make_cube();
    CHECK(m.vertices.size() == 24);
    CHECK(m.indices.size() == 36);
}

TEST_CASE("make_cube positions are unit cube corners") {
    const auto m = make_cube();
    for (const auto& v : m.vertices) {
        CHECK(std::abs(v.position.x) == doctest::Approx(1.0));
        CHECK(std::abs(v.position.y) == doctest::Approx(1.0));
        CHECK(std::abs(v.position.z) == doctest::Approx(1.0));
    }
}

TEST_CASE("make_cube normals are unit-length and axis-aligned") {
    const auto m = make_cube();
    for (const auto& v : m.vertices) {
        const float l = std::sqrt(v.normal.x * v.normal.x +
                                   v.normal.y * v.normal.y +
                                   v.normal.z * v.normal.z);
        CHECK(l == doctest::Approx(1.0));
        // exactly one component is +/-1, others zero (axis-aligned)
        const int nonzero =
            (v.normal.x != 0.0f) + (v.normal.y != 0.0f) + (v.normal.z != 0.0f);
        CHECK(nonzero == 1);
    }
}

TEST_CASE("make_cube indices reference valid vertex slots") {
    const auto m = make_cube();
    for (auto i : m.indices) {
        CHECK(i < m.vertices.size());
    }
}

TEST_CASE("make_cube triangles wind CCW when viewed from outside") {
    const auto m = make_cube();
    for (std::size_t t = 0; t < m.indices.size(); t += 3) {
        const auto& a = m.vertices[m.indices[t + 0]];
        const auto& b = m.vertices[m.indices[t + 1]];
        const auto& c = m.vertices[m.indices[t + 2]];
        // The three vertices of a face share the same normal (flat face).
        CHECK(a.normal.x == b.normal.x);
        CHECK(a.normal.y == b.normal.y);
        CHECK(a.normal.z == b.normal.z);

        const float ux = b.position.x - a.position.x;
        const float uy = b.position.y - a.position.y;
        const float uz = b.position.z - a.position.z;
        const float vx = c.position.x - a.position.x;
        const float vy = c.position.y - a.position.y;
        const float vz = c.position.z - a.position.z;
        // Cross product of (b-a) x (c-a) should align with the stored normal
        // (positive dot product) when winding is CCW seen from outside.
        const float cx = uy * vz - uz * vy;
        const float cy = uz * vx - ux * vz;
        const float cz = ux * vy - uy * vx;
        const float d  = cx * a.normal.x + cy * a.normal.y + cz * a.normal.z;
        CHECK(d > 0.0f);
    }
}
