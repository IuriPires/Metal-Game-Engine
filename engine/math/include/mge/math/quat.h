#pragma once

#include "mge/math/constants.h"
#include "mge/math/mat.h"
#include "mge/math/vec.h"

#include <cmath>

namespace mge::math {

// xyz + w convention. Identity = (0, 0, 0, 1). Composition uses Hamilton
// product (right-multiplication applies first).
struct alignas(16) Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    constexpr Quat() = default;
    constexpr Quat(float xx, float yy, float zz, float ww) noexcept
        : x(xx), y(yy), z(zz), w(ww) {}

    [[nodiscard]] static constexpr Quat identity() noexcept { return {}; }

    // Axis must be normalized; angle in radians.
    [[nodiscard]] static inline Quat from_axis_angle(Vec3 axis, float radians) noexcept {
        const float h = radians * 0.5f;
        const float s = std::sin(h);
        return {axis.x * s, axis.y * s, axis.z * s, std::cos(h)};
    }
};

[[nodiscard]] constexpr float dot(Quat a, Quat b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

[[nodiscard]] inline float length(Quat q) noexcept { return std::sqrt(dot(q, q)); }

[[nodiscard]] inline Quat normalize(Quat q) noexcept {
    const float l = length(q);
    return l > 0.0f ? Quat{q.x / l, q.y / l, q.z / l, q.w / l} : Quat::identity();
}

[[nodiscard]] inline Quat conjugate(Quat q) noexcept { return {-q.x, -q.y, -q.z, q.w}; }

// Hamilton product: (a * b) applies b first then a to a vector.
[[nodiscard]] inline Quat operator*(Quat a, Quat b) noexcept {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

// Rotate a vector. Equivalent to (q * (v,0) * q^-1) but cheaper.
[[nodiscard]] inline Vec3 rotate(Quat q, Vec3 v) noexcept {
    const Vec3 u{q.x, q.y, q.z};
    const float s = q.w;
    return u * (2.0f * dot(u, v))
         + v * (s * s - dot(u, u))
         + cross(u, v) * (2.0f * s);
}

// Shortest-path slerp. t in [0, 1].
[[nodiscard]] inline Quat slerp(Quat a, Quat b, float t) noexcept {
    float d = dot(a, b);
    if (d < 0.0f) {
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    constexpr float threshold = 0.9995f;
    if (d > threshold) {
        // Near-identical orientations: linear interp + normalize.
        Quat r{
            a.x + t * (b.x - a.x),
            a.y + t * (b.y - a.y),
            a.z + t * (b.z - a.z),
            a.w + t * (b.w - a.w),
        };
        return normalize(r);
    }
    const float theta_0     = std::acos(d);
    const float theta       = theta_0 * t;
    const float sin_theta   = std::sin(theta);
    const float sin_theta_0 = std::sin(theta_0);
    const float wa          = std::cos(theta) - d * sin_theta / sin_theta_0;
    const float wb          = sin_theta / sin_theta_0;
    return {
        wa * a.x + wb * b.x,
        wa * a.y + wb * b.y,
        wa * a.z + wb * b.z,
        wa * a.w + wb * b.w,
    };
}

[[nodiscard]] inline Mat4 to_mat4(Quat q) noexcept {
    const float xx = q.x * q.x;
    const float yy = q.y * q.y;
    const float zz = q.z * q.z;
    const float xy = q.x * q.y;
    const float xz = q.x * q.z;
    const float yz = q.y * q.z;
    const float wx = q.w * q.x;
    const float wy = q.w * q.y;
    const float wz = q.w * q.z;

    Mat4 m;
    m.cols[0] = Vec4{1.0f - 2.0f * (yy + zz),  2.0f * (xy + wz),       2.0f * (xz - wy),       0};
    m.cols[1] = Vec4{2.0f * (xy - wz),         1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx),       0};
    m.cols[2] = Vec4{2.0f * (xz + wy),         2.0f * (yz - wx),        1.0f - 2.0f * (xx + yy), 0};
    m.cols[3] = Vec4{0, 0, 0, 1};
    return m;
}

static_assert(sizeof(Quat)  == 16);
static_assert(alignof(Quat) == 16);

}  // namespace mge::math
