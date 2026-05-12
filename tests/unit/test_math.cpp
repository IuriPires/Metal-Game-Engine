#include "mge/math/aabb.h"
#include "mge/math/constants.h"
#include "mge/math/mat.h"
#include "mge/math/quat.h"
#include "mge/math/vec.h"

#include <doctest/doctest.h>

#include <cmath>

using namespace mge::math;

namespace {

bool mat_near(const Mat4& a, const Mat4& b, float tol = 1e-4f) {
    for (std::size_t c = 0; c < 4; ++c) {
        for (std::size_t r = 0; r < 4; ++r) {
            if (std::abs(a[c][r] - b[c][r]) > tol) {
                return false;
            }
        }
    }
    return true;
}

bool vec_near(Vec3 a, Vec3 b, float tol = 1e-4f) {
    return std::abs(a.x - b.x) <= tol && std::abs(a.y - b.y) <= tol &&
           std::abs(a.z - b.z) <= tol;
}

}  // namespace

TEST_CASE("Vec ops") {
    Vec3 a{1, 2, 3};
    Vec3 b{4, 5, 6};
    CHECK(dot(a, b) == doctest::Approx(32.0));
    CHECK(vec_near(cross(Vec3{1, 0, 0}, Vec3{0, 1, 0}), Vec3{0, 0, 1}));
    CHECK(length(Vec3{3, 0, 0}) == doctest::Approx(3.0));
    CHECK(vec_near(normalize(Vec3{0, 0, 5}), Vec3{0, 0, 1}));
}

TEST_CASE("Mat4 identity is multiplicative identity") {
    const Mat4 i = Mat4::identity();
    const Mat4 m = translation(Vec3{1, 2, 3}) * rotation_y(0.5f) * scale(Vec3{2, 3, 4});
    CHECK(mat_near(i * m, m));
    CHECK(mat_near(m * i, m));
}

TEST_CASE("Mat4 transpose involution") {
    Mat4 m;
    int  k = 0;
    for (std::size_t c = 0; c < 4; ++c) {
        for (std::size_t r = 0; r < 4; ++r) {
            m[c][r] = static_cast<float>(++k);
        }
    }
    CHECK(mat_near(transpose(transpose(m)), m));
}

TEST_CASE("Mat4 NEON path matches scalar path") {
    Mat4 a;
    Mat4 b;
    int  k = 0;
    for (std::size_t c = 0; c < 4; ++c) {
        for (std::size_t r = 0; r < 4; ++r) {
            a[c][r] = static_cast<float>(++k);
            b[c][r] = static_cast<float>(static_cast<float>(k) * 0.5f + 1.0f);
        }
    }
#if MGE_ARCH_NEON
    const Mat4 s = mul_scalar(a, b);
    const Mat4 n = mul_neon(a, b);
    CHECK(mat_near(s, n, 1e-3f));
#else
    CHECK(true);  // path not active on this arch
#endif
}

TEST_CASE("Mat4 inverse round-trip") {
    const Mat4 m = translation(Vec3{1, 2, 3}) * rotation_z(0.7f) * scale(Vec3{2, 3, 4});
    const Mat4 inv = inverse(m);
    CHECK(mat_near(m * inv, Mat4::identity(), 1e-3f));
    CHECK(mat_near(inv * m, Mat4::identity(), 1e-3f));
}

TEST_CASE("Mat4 transform of point works") {
    const Mat4 m = translation(Vec3{10, 20, 30});
    const Vec4 p = m * Vec4{1, 2, 3, 1};
    CHECK(p.x == doctest::Approx(11.0));
    CHECK(p.y == doctest::Approx(22.0));
    CHECK(p.z == doctest::Approx(33.0));
    CHECK(p.w == doctest::Approx(1.0));
}

TEST_CASE("Quat identity does not rotate") {
    const Quat q = Quat::identity();
    CHECK(vec_near(rotate(q, Vec3{1, 2, 3}), Vec3{1, 2, 3}));
}

TEST_CASE("Quat 90 degrees around Y rotates X to -Z") {
    const Quat q = Quat::from_axis_angle(Vec3{0, 1, 0}, half_pi);
    CHECK(vec_near(rotate(q, Vec3{1, 0, 0}), Vec3{0, 0, -1}, 1e-4f));
}

TEST_CASE("Quat to_mat4 matches rotate()") {
    const Quat q = Quat::from_axis_angle(normalize(Vec3{1, 1, 0}), 0.7f);
    const Vec3 v{0.3f, 0.6f, 0.9f};
    const Vec3 from_quat = rotate(q, v);
    const Mat4 m         = to_mat4(q);
    const Vec4 from_mat  = m * Vec4{v.x, v.y, v.z, 0};
    CHECK(vec_near(Vec3{from_mat.x, from_mat.y, from_mat.z}, from_quat, 1e-4f));
}

TEST_CASE("Quat slerp endpoints") {
    const Quat a = Quat::identity();
    const Quat b = Quat::from_axis_angle(Vec3{0, 0, 1}, pi);
    const Quat s0 = slerp(a, b, 0.0f);
    const Quat s1 = slerp(a, b, 1.0f);
    CHECK(std::abs(s0.w - 1.0f) < 1e-4f);
    CHECK(std::abs(std::abs(s1.w) - 0.0f) < 1e-4f);
}

TEST_CASE("Aabb expand and contains") {
    Aabb box = Aabb::empty();
    CHECK_FALSE(box.is_valid());
    box.expand(Vec3{1, 1, 1});
    box.expand(Vec3{-1, -1, -1});
    CHECK(box.is_valid());
    CHECK(box.contains(Vec3{0, 0, 0}));
    CHECK_FALSE(box.contains(Vec3{2, 0, 0}));
}

TEST_CASE("Aabb intersect") {
    const Aabb a = Aabb::from_points(Vec3{0, 0, 0}, Vec3{1, 1, 1});
    const Aabb b = Aabb::from_points(Vec3{0.5f, 0.5f, 0.5f}, Vec3{2, 2, 2});
    const Aabb c = Aabb::from_points(Vec3{2, 2, 2}, Vec3{3, 3, 3});
    CHECK(a.intersects(b));
    CHECK_FALSE(a.intersects(c));
}

TEST_CASE("ABI layout matches Metal expectations") {
    static_assert(sizeof(Vec2) == 8);
    static_assert(sizeof(Vec3) == 16);
    static_assert(sizeof(Vec4) == 16);
    static_assert(sizeof(Mat4) == 64);
    static_assert(sizeof(Quat) == 16);
    static_assert(alignof(Mat4) == 16);
    CHECK(true);
}
