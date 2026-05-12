#pragma once

#include <cmath>

namespace mge::math {

inline constexpr float pi      = 3.14159265358979323846f;
inline constexpr float two_pi  = 2.0f * pi;
inline constexpr float half_pi = 0.5f * pi;
inline constexpr float deg2rad = pi / 180.0f;
inline constexpr float rad2deg = 180.0f / pi;
inline constexpr float epsilon = 1e-5f;

[[nodiscard]] constexpr float radians(float deg) noexcept { return deg * deg2rad; }
[[nodiscard]] constexpr float degrees(float rad) noexcept { return rad * rad2deg; }

[[nodiscard]] inline bool nearly_equal(float a, float b, float tol = epsilon) noexcept {
    return std::abs(a - b) <= tol;
}

}  // namespace mge::math
