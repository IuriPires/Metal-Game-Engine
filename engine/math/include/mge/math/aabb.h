#pragma once

#include "mge/math/vec.h"

#include <algorithm>
#include <limits>

namespace mge::math {

struct Aabb {
    Vec3 min{ std::numeric_limits<float>::infinity(),
              std::numeric_limits<float>::infinity(),
              std::numeric_limits<float>::infinity()};
    Vec3 max{-std::numeric_limits<float>::infinity(),
             -std::numeric_limits<float>::infinity(),
             -std::numeric_limits<float>::infinity()};

    [[nodiscard]] static constexpr Aabb empty() noexcept { return {}; }

    [[nodiscard]] static constexpr Aabb from_points(Vec3 a, Vec3 b) noexcept {
        return {
            Vec3{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)},
            Vec3{std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)},
        };
    }

    void expand(Vec3 p) noexcept {
        min = Vec3{std::min(min.x, p.x), std::min(min.y, p.y), std::min(min.z, p.z)};
        max = Vec3{std::max(max.x, p.x), std::max(max.y, p.y), std::max(max.z, p.z)};
    }

    void expand(const Aabb& o) noexcept {
        expand(o.min);
        expand(o.max);
    }

    [[nodiscard]] constexpr Vec3 center() const noexcept {
        return Vec3{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f};
    }

    [[nodiscard]] constexpr Vec3 extent() const noexcept {
        return Vec3{(max.x - min.x) * 0.5f, (max.y - min.y) * 0.5f, (max.z - min.z) * 0.5f};
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return min.x <= max.x && min.y <= max.y && min.z <= max.z;
    }

    [[nodiscard]] constexpr bool contains(Vec3 p) const noexcept {
        return p.x >= min.x && p.x <= max.x
            && p.y >= min.y && p.y <= max.y
            && p.z >= min.z && p.z <= max.z;
    }

    [[nodiscard]] constexpr bool intersects(const Aabb& o) const noexcept {
        return !(o.max.x < min.x || o.min.x > max.x
              || o.max.y < min.y || o.min.y > max.y
              || o.max.z < min.z || o.min.z > max.z);
    }
};

}  // namespace mge::math
