#pragma once

#include "mge/math/arch.h"
#include "mge/math/constants.h"
#include "mge/math/vec.h"

#include <cmath>
#include <cstddef>

namespace mge::math {

// Column-major matrices, matching MSL `float4x4` semantics. `cols[0]` is the
// first column (basis x). Translation lives in `cols[3].xyz`. This is the
// same convention as Apple's simd_float4x4 and as GLSL.

struct alignas(16) Mat3 {
    Vec3 cols[3]{Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};

    [[nodiscard]] constexpr Vec3& operator[](std::size_t c) noexcept { return cols[c]; }
    [[nodiscard]] constexpr const Vec3& operator[](std::size_t c) const noexcept {
        return cols[c];
    }

    [[nodiscard]] static constexpr Mat3 identity() noexcept { return {}; }
};

struct alignas(16) Mat4 {
    Vec4 cols[4]{Vec4{1, 0, 0, 0}, Vec4{0, 1, 0, 0}, Vec4{0, 0, 1, 0}, Vec4{0, 0, 0, 1}};

    [[nodiscard]] constexpr Vec4& operator[](std::size_t c) noexcept { return cols[c]; }
    [[nodiscard]] constexpr const Vec4& operator[](std::size_t c) const noexcept {
        return cols[c];
    }

    [[nodiscard]] static constexpr Mat4 identity() noexcept { return {}; }
};

// ---- Mat4 ops ----

[[nodiscard]] inline Mat4 transpose(const Mat4& m) noexcept {
    Mat4 r;
    for (std::size_t c = 0; c < 4; ++c) {
        for (std::size_t rr = 0; rr < 4; ++rr) {
            r.cols[c][rr] = m.cols[rr][c];
        }
    }
    return r;
}

[[nodiscard]] inline Mat4 mul_scalar(const Mat4& a, const Mat4& b) noexcept {
    Mat4 out;
    for (std::size_t c = 0; c < 4; ++c) {
        Vec4 col{};
        for (std::size_t k = 0; k < 4; ++k) {
            col = col + a.cols[k] * b.cols[c][k];
        }
        out.cols[c] = col;
    }
    return out;
}

#if MGE_ARCH_NEON
[[nodiscard]] inline Mat4 mul_neon(const Mat4& a, const Mat4& b) noexcept {
    // Load A's columns as float32x4_t.
    const float32x4_t a0 = vld1q_f32(&a.cols[0].x);
    const float32x4_t a1 = vld1q_f32(&a.cols[1].x);
    const float32x4_t a2 = vld1q_f32(&a.cols[2].x);
    const float32x4_t a3 = vld1q_f32(&a.cols[3].x);

    Mat4 out;
    for (std::size_t c = 0; c < 4; ++c) {
        const float32x4_t bc = vld1q_f32(&b.cols[c].x);
        float32x4_t       r  = vmulq_laneq_f32(a0, bc, 0);
        r                    = vfmaq_laneq_f32(r, a1, bc, 1);
        r                    = vfmaq_laneq_f32(r, a2, bc, 2);
        r                    = vfmaq_laneq_f32(r, a3, bc, 3);
        vst1q_f32(&out.cols[c].x, r);
    }
    return out;
}
#endif

[[nodiscard]] inline Mat4 operator*(const Mat4& a, const Mat4& b) noexcept {
#if MGE_ARCH_NEON
    return mul_neon(a, b);
#else
    return mul_scalar(a, b);
#endif
}

[[nodiscard]] inline Vec4 operator*(const Mat4& m, Vec4 v) noexcept {
#if MGE_ARCH_NEON
    const float32x4_t c0 = vld1q_f32(&m.cols[0].x);
    const float32x4_t c1 = vld1q_f32(&m.cols[1].x);
    const float32x4_t c2 = vld1q_f32(&m.cols[2].x);
    const float32x4_t c3 = vld1q_f32(&m.cols[3].x);
    const float32x4_t vv = vld1q_f32(&v.x);
    float32x4_t       r  = vmulq_laneq_f32(c0, vv, 0);
    r                    = vfmaq_laneq_f32(r, c1, vv, 1);
    r                    = vfmaq_laneq_f32(r, c2, vv, 2);
    r                    = vfmaq_laneq_f32(r, c3, vv, 3);
    Vec4 out;
    vst1q_f32(&out.x, r);
    return out;
#else
    return m.cols[0] * v.x + m.cols[1] * v.y + m.cols[2] * v.z + m.cols[3] * v.w;
#endif
}

// ---- Factories ----

[[nodiscard]] inline Mat4 translation(Vec3 t) noexcept {
    Mat4 m;
    m.cols[3] = Vec4{t.x, t.y, t.z, 1.0f};
    return m;
}

[[nodiscard]] inline Mat4 scale(Vec3 s) noexcept {
    Mat4 m;
    m.cols[0] = Vec4{s.x, 0, 0, 0};
    m.cols[1] = Vec4{0, s.y, 0, 0};
    m.cols[2] = Vec4{0, 0, s.z, 0};
    return m;
}

[[nodiscard]] inline Mat4 rotation_x(float radians) noexcept {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4        m;
    m.cols[1] = Vec4{0,  c, s, 0};
    m.cols[2] = Vec4{0, -s, c, 0};
    return m;
}

[[nodiscard]] inline Mat4 rotation_y(float radians) noexcept {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4        m;
    m.cols[0] = Vec4{c, 0, -s, 0};
    m.cols[2] = Vec4{s, 0,  c, 0};
    return m;
}

[[nodiscard]] inline Mat4 rotation_z(float radians) noexcept {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4        m;
    m.cols[0] = Vec4{ c, s, 0, 0};
    m.cols[1] = Vec4{-s, c, 0, 0};
    return m;
}

// Right-handed look-at producing a view matrix that takes world-space points
// into camera space where -Z is forward (Metal convention).
[[nodiscard]] inline Mat4 look_at_rh(Vec3 eye, Vec3 center, Vec3 up) noexcept {
    const Vec3 f = normalize(center - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);

    Mat4 m;
    m.cols[0] = Vec4{ s.x,  u.x, -f.x, 0};
    m.cols[1] = Vec4{ s.y,  u.y, -f.y, 0};
    m.cols[2] = Vec4{ s.z,  u.z, -f.z, 0};
    m.cols[3] = Vec4{-dot(s, eye), -dot(u, eye), dot(f, eye), 1};
    return m;
}

// Right-handed perspective projection mapping NDC z into [0, 1] (Metal /
// Vulkan / D3D convention). Useful for reverse-Z (call with swapped near/far).
[[nodiscard]] inline Mat4 perspective_rh_zo(float fovy_rad, float aspect, float zn,
                                            float zf) noexcept {
    const float f       = 1.0f / std::tan(fovy_rad * 0.5f);
    Mat4        m;
    m.cols[0] = Vec4{f / aspect, 0, 0, 0};
    m.cols[1] = Vec4{0, f, 0, 0};
    m.cols[2] = Vec4{0, 0, zf / (zn - zf), -1};
    m.cols[3] = Vec4{0, 0, (zn * zf) / (zn - zf), 0};
    return m;
}

// 4x4 determinant + adjugate-based inverse. Slow path - for hot rendering
// matrices prefer composing the inverse manually (e.g. view = inverse(model)).
[[nodiscard]] inline Mat4 inverse(const Mat4& m) noexcept {
    const float* a = &m.cols[0].x;
    Mat4         inv;
    float*       o = &inv.cols[0].x;

    o[0]  =  a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
    o[1]  = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
    o[2]  =  a[1]*a[6]*a[15]  - a[1]*a[7]*a[14]  - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7]  - a[13]*a[3]*a[6];
    o[3]  = -a[1]*a[6]*a[11]  + a[1]*a[7]*a[10]  + a[5]*a[2]*a[11] - a[5]*a[3]*a[10] - a[9]*a[2]*a[7]   + a[9]*a[3]*a[6];

    o[4]  = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
    o[5]  =  a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
    o[6]  = -a[0]*a[6]*a[15]  + a[0]*a[7]*a[14]  + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7]  + a[12]*a[3]*a[6];
    o[7]  =  a[0]*a[6]*a[11]  - a[0]*a[7]*a[10]  - a[4]*a[2]*a[11] + a[4]*a[3]*a[10] + a[8]*a[2]*a[7]   - a[8]*a[3]*a[6];

    o[8]  =  a[4]*a[9]*a[15]  - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
    o[9]  = -a[0]*a[9]*a[15]  + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
    o[10] =  a[0]*a[5]*a[15]  - a[0]*a[7]*a[13]  - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7]  - a[12]*a[3]*a[5];
    o[11] = -a[0]*a[5]*a[11]  + a[0]*a[7]*a[9]   + a[4]*a[1]*a[11] - a[4]*a[3]*a[9]  - a[8]*a[1]*a[7]   + a[8]*a[3]*a[5];

    o[12] = -a[4]*a[9]*a[14]  + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
    o[13] =  a[0]*a[9]*a[14]  - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
    o[14] = -a[0]*a[5]*a[14]  + a[0]*a[6]*a[13]  + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6]  + a[12]*a[2]*a[5];
    o[15] =  a[0]*a[5]*a[10]  - a[0]*a[6]*a[9]   - a[4]*a[1]*a[10] + a[4]*a[2]*a[9]  + a[8]*a[1]*a[6]   - a[8]*a[2]*a[5];

    float det = a[0]*o[0] + a[1]*o[4] + a[2]*o[8] + a[3]*o[12];
    if (det == 0.0f) {
        return Mat4::identity();
    }
    det = 1.0f / det;
    for (int i = 0; i < 16; ++i) {
        o[i] *= det;
    }
    return inv;
}

// ABI: matches MSL float4x4 (64 B, 16 B aligned).
static_assert(sizeof(Mat4)  == 64);
static_assert(alignof(Mat4) == 16);

}  // namespace mge::math
