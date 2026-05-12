#pragma once

#include "mge/math/constants.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace mge::math {

// Layout note: Vec3 is padded to 16 bytes so it matches MSL's `packed_float3`
// when stored in a 16-byte slot. This pads-then-aligns is the common Metal
// constant-buffer arrangement. Static asserts at bottom of file confirm.

struct alignas(8) Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float xx, float yy) noexcept : x(xx), y(yy) {}

    [[nodiscard]] constexpr float operator[](std::size_t i) const noexcept {
        return (&x)[i];
    }
    constexpr float& operator[](std::size_t i) noexcept { return (&x)[i]; }
};

struct alignas(16) Vec3 {
    float x        = 0.0f;
    float y        = 0.0f;
    float z        = 0.0f;
    float _padding = 0.0f;  // explicit pad for ABI parity with float4 slots

    constexpr Vec3() = default;
    constexpr Vec3(float xx, float yy, float zz) noexcept : x(xx), y(yy), z(zz), _padding(0.0f) {}

    [[nodiscard]] constexpr float operator[](std::size_t i) const noexcept {
        return (&x)[i];
    }
    constexpr float& operator[](std::size_t i) noexcept { return (&x)[i]; }
};

struct alignas(16) Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    constexpr Vec4() = default;
    constexpr Vec4(float xx, float yy, float zz, float ww) noexcept
        : x(xx), y(yy), z(zz), w(ww) {}
    constexpr Vec4(Vec3 v, float ww) noexcept : x(v.x), y(v.y), z(v.z), w(ww) {}

    [[nodiscard]] constexpr float operator[](std::size_t i) const noexcept {
        return (&x)[i];
    }
    constexpr float& operator[](std::size_t i) noexcept { return (&x)[i]; }
};

// ----- Vec2 ops -----
[[nodiscard]] constexpr Vec2 operator+(Vec2 a, Vec2 b) noexcept { return {a.x + b.x, a.y + b.y}; }
[[nodiscard]] constexpr Vec2 operator-(Vec2 a, Vec2 b) noexcept { return {a.x - b.x, a.y - b.y}; }
[[nodiscard]] constexpr Vec2 operator*(Vec2 a, float s) noexcept { return {a.x * s, a.y * s}; }
[[nodiscard]] constexpr Vec2 operator*(float s, Vec2 a) noexcept { return a * s; }
[[nodiscard]] constexpr Vec2 operator-(Vec2 a) noexcept { return {-a.x, -a.y}; }
[[nodiscard]] constexpr float dot(Vec2 a, Vec2 b) noexcept { return a.x * b.x + a.y * b.y; }
[[nodiscard]] inline float    length(Vec2 a) noexcept { return std::sqrt(dot(a, a)); }
[[nodiscard]] inline Vec2     normalize(Vec2 a) noexcept {
    const float l = length(a);
    return l > 0.0f ? Vec2{a.x / l, a.y / l} : Vec2{};
}

// ----- Vec3 ops -----
[[nodiscard]] constexpr Vec3 operator+(Vec3 a, Vec3 b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
[[nodiscard]] constexpr Vec3 operator-(Vec3 a, Vec3 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
[[nodiscard]] constexpr Vec3 operator*(Vec3 a, float s) noexcept {
    return {a.x * s, a.y * s, a.z * s};
}
[[nodiscard]] constexpr Vec3 operator*(float s, Vec3 a) noexcept { return a * s; }
[[nodiscard]] constexpr Vec3 operator-(Vec3 a) noexcept { return {-a.x, -a.y, -a.z}; }
[[nodiscard]] constexpr float dot(Vec3 a, Vec3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
[[nodiscard]] constexpr Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}
[[nodiscard]] inline float length(Vec3 a) noexcept { return std::sqrt(dot(a, a)); }
[[nodiscard]] inline Vec3  normalize(Vec3 a) noexcept {
    const float l = length(a);
    return l > 0.0f ? Vec3{a.x / l, a.y / l, a.z / l} : Vec3{};
}

// ----- Vec4 ops -----
[[nodiscard]] constexpr Vec4 operator+(Vec4 a, Vec4 b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}
[[nodiscard]] constexpr Vec4 operator-(Vec4 a, Vec4 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
}
[[nodiscard]] constexpr Vec4 operator*(Vec4 a, float s) noexcept {
    return {a.x * s, a.y * s, a.z * s, a.w * s};
}
[[nodiscard]] constexpr Vec4 operator*(float s, Vec4 a) noexcept { return a * s; }
[[nodiscard]] constexpr Vec4 operator-(Vec4 a) noexcept { return {-a.x, -a.y, -a.z, -a.w}; }
[[nodiscard]] constexpr float dot(Vec4 a, Vec4 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}
[[nodiscard]] inline float length(Vec4 a) noexcept { return std::sqrt(dot(a, a)); }
[[nodiscard]] inline Vec4  normalize(Vec4 a) noexcept {
    const float l = length(a);
    return l > 0.0f ? Vec4{a.x / l, a.y / l, a.z / l, a.w / l} : Vec4{};
}

// ABI contracts: keep these matching Metal's packed/aligned float types.
static_assert(sizeof(Vec2)  == 8,  "Vec2 size must equal float2 (8 B)");
static_assert(alignof(Vec2) == 8);
static_assert(sizeof(Vec3)  == 16, "Vec3 size must equal float4 slot (16 B)");
static_assert(alignof(Vec3) == 16);
static_assert(sizeof(Vec4)  == 16, "Vec4 size must equal float4 (16 B)");
static_assert(alignof(Vec4) == 16);

}  // namespace mge::math
