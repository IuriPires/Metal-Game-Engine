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

// Picking primitive — a half-line in world space. `dir` is expected to be
// unit length but isn't required to be; intersection functions compute their
// own `t` scaled by the input direction's length.
struct Ray {
    Vec3 origin{0.0f, 0.0f, 0.0f};
    Vec3 dir{0.0f, 0.0f, -1.0f};
};

// Slab-method ray-vs-AABB intersection. Returns hit + nearest hit `t_near`
// (≥ 0 when hit). When the ray origin is inside the box, `t_near` is 0.
//
// Branch-free over the three axes; the only branches are the early-out on
// "ray parallel to the slab while origin outside the slab" and the final
// hit predicate.
struct RayAabbHit {
    bool  hit    = false;
    float t_near = 0.0f;
};

[[nodiscard]] inline RayAabbHit ray_aabb_intersect(const Ray& r, const Aabb& b) noexcept {
    float t_min = 0.0f;
    float t_max = std::numeric_limits<float>::infinity();
    for (int i = 0; i < 3; ++i) {
        const float o = (&r.origin.x)[i];
        const float d = (&r.dir.x)[i];
        const float lo = (&b.min.x)[i];
        const float hi = (&b.max.x)[i];
        if (std::abs(d) < 1e-8f) {
            // Ray is parallel to this slab; miss unless origin lies between
            // the slab planes.
            if (o < lo || o > hi) return {false, 0.0f};
            continue;
        }
        const float inv = 1.0f / d;
        float t0 = (lo - o) * inv;
        float t1 = (hi - o) * inv;
        if (t0 > t1) std::swap(t0, t1);
        if (t0 > t_min) t_min = t0;
        if (t1 < t_max) t_max = t1;
        if (t_min > t_max) return {false, 0.0f};
    }
    // t_max must be ≥ 0 too — otherwise the AABB is fully behind the ray.
    if (t_max < 0.0f) return {false, 0.0f};
    return {true, t_min};
}

}  // namespace mge::math
