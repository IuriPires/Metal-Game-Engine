#pragma once

#include "mge/math/aabb.h"
#include "mge/math/arch.h"
#include "mge/math/mat.h"
#include "mge/math/vec.h"

#include <cmath>
#include <cstdint>

namespace mge::math {

// Plane in implicit form: dot(normal, p) + d = 0. Points with positive sign
// are on the "inside" half-space (the side normal points to).
struct alignas(16) Plane {
    Vec3  normal{0, 0, 1};
    float d = 0.0f;
};

[[nodiscard]] inline Plane normalize(Plane p) noexcept {
    const float len = std::sqrt(p.normal.x * p.normal.x + p.normal.y * p.normal.y +
                                 p.normal.z * p.normal.z);
    if (len <= 0.0f) return p;
    const float inv = 1.0f / len;
    return Plane{Vec3{p.normal.x * inv, p.normal.y * inv, p.normal.z * inv}, p.d * inv};
}

[[nodiscard]] inline float signed_distance(const Plane& p, Vec3 q) noexcept {
    return p.normal.x * q.x + p.normal.y * q.y + p.normal.z * q.z + p.d;
}

// 6-plane view frustum. Plane normals point INWARD; a point is inside the
// frustum iff signed_distance(plane, point) >= 0 for all 6 planes.
struct Frustum {
    enum PlaneId { Left = 0, Right, Bottom, Top, Near, Far, PlaneCount = 6 };
    Plane planes[6];

    // Extract the frustum planes from a column-major view-projection matrix
    // mapping NDC z into [0, 1] (Metal / D3D / Vulkan convention). Planes are
    // normalized so signed_distance returns a true distance in world units.
    [[nodiscard]] static Frustum from_view_projection(const Mat4& vp) noexcept {
        // Row i of vp expressed via the column-major layout: vp[c][i] for col c.
        auto row = [&](int i) {
            return Vec4{vp[0][static_cast<std::size_t>(i)],
                        vp[1][static_cast<std::size_t>(i)],
                        vp[2][static_cast<std::size_t>(i)],
                        vp[3][static_cast<std::size_t>(i)]};
        };
        const Vec4 r0 = row(0);
        const Vec4 r1 = row(1);
        const Vec4 r2 = row(2);
        const Vec4 r3 = row(3);

        auto as_plane = [](Vec4 v) {
            return Plane{Vec3{v.x, v.y, v.z}, v.w};
        };

        Frustum f;
        f.planes[Left]   = normalize(as_plane(Vec4{r3.x + r0.x, r3.y + r0.y, r3.z + r0.z, r3.w + r0.w}));
        f.planes[Right]  = normalize(as_plane(Vec4{r3.x - r0.x, r3.y - r0.y, r3.z - r0.z, r3.w - r0.w}));
        f.planes[Bottom] = normalize(as_plane(Vec4{r3.x + r1.x, r3.y + r1.y, r3.z + r1.z, r3.w + r1.w}));
        f.planes[Top]    = normalize(as_plane(Vec4{r3.x - r1.x, r3.y - r1.y, r3.z - r1.z, r3.w - r1.w}));
        // [0,1] depth: near plane = row 2 directly; far plane = row3 - row2.
        f.planes[Near]   = normalize(as_plane(r2));
        f.planes[Far]    = normalize(as_plane(Vec4{r3.x - r2.x, r3.y - r2.y, r3.z - r2.z, r3.w - r2.w}));
        return f;
    }
};

// Result of culling an AABB against a frustum.
enum class CullResult : std::uint8_t {
    Outside     = 0,
    Inside      = 1,  // fully inside, no plane crosses the AABB
    Intersecting = 2,
};

// Scalar AABB-vs-frustum test using the "positive vertex" optimization.
[[nodiscard]] inline CullResult test_aabb(const Frustum& f, const Aabb& box) noexcept {
    bool intersects = false;
    for (std::size_t i = 0; i < Frustum::PlaneCount; ++i) {
        const Plane& p = f.planes[i];
        // Positive vertex: the AABB corner farthest along the plane normal.
        Vec3 pv{
            p.normal.x >= 0 ? box.max.x : box.min.x,
            p.normal.y >= 0 ? box.max.y : box.min.y,
            p.normal.z >= 0 ? box.max.z : box.min.z,
        };
        if (signed_distance(p, pv) < 0.0f) {
            return CullResult::Outside;  // entirely behind this plane
        }
        Vec3 nv{
            p.normal.x >= 0 ? box.min.x : box.max.x,
            p.normal.y >= 0 ? box.min.y : box.max.y,
            p.normal.z >= 0 ? box.min.z : box.max.z,
        };
        if (signed_distance(p, nv) < 0.0f) {
            intersects = true;  // straddles this plane
        }
    }
    return intersects ? CullResult::Intersecting : CullResult::Inside;
}

// Fast "visible-or-not" wrapper. True if any part of the AABB is inside.
[[nodiscard]] inline bool aabb_visible(const Frustum& f, const Aabb& box) noexcept {
    return test_aabb(f, box) != CullResult::Outside;
}

#if MGE_ARCH_NEON
// NEON 4-wide path: tests four AABBs against the frustum simultaneously. The
// AABBs are passed as separate min/max arrays in struct-of-arrays form so the
// loads vectorize cleanly. Output: visible[i] = 1 if AABB i is at least
// partially in the frustum, else 0.
inline void aabb_visible_x4_neon(const Frustum& f,
                                  const float min_x[4], const float min_y[4], const float min_z[4],
                                  const float max_x[4], const float max_y[4], const float max_z[4],
                                  std::uint32_t visible[4]) noexcept {
    const float32x4_t mnx = vld1q_f32(min_x);
    const float32x4_t mny = vld1q_f32(min_y);
    const float32x4_t mnz = vld1q_f32(min_z);
    const float32x4_t mxx = vld1q_f32(max_x);
    const float32x4_t mxy = vld1q_f32(max_y);
    const float32x4_t mxz = vld1q_f32(max_z);

    uint32x4_t outside = vdupq_n_u32(0);  // bit set per lane when behind any plane

    for (std::size_t i = 0; i < Frustum::PlaneCount; ++i) {
        const Plane& p   = f.planes[i];
        const float32x4_t nx = vdupq_n_f32(p.normal.x);
        const float32x4_t ny = vdupq_n_f32(p.normal.y);
        const float32x4_t nz = vdupq_n_f32(p.normal.z);
        const float32x4_t d  = vdupq_n_f32(p.d);
        // Positive vertex per lane: pick max if normal >= 0 else min.
        const uint32x4_t px = vcgezq_f32(nx);
        const uint32x4_t py = vcgezq_f32(ny);
        const uint32x4_t pz = vcgezq_f32(nz);
        const float32x4_t vx = vbslq_f32(px, mxx, mnx);
        const float32x4_t vy = vbslq_f32(py, mxy, mny);
        const float32x4_t vz = vbslq_f32(pz, mxz, mnz);
        // dist = nx*vx + ny*vy + nz*vz + d
        float32x4_t dist = vmlaq_f32(d, nx, vx);
        dist             = vmlaq_f32(dist, ny, vy);
        dist             = vmlaq_f32(dist, nz, vz);
        // If positive vertex is behind, AABB is fully outside. Tag the lane.
        outside = vorrq_u32(outside, vcltq_f32(dist, vdupq_n_f32(0.0f)));
    }
    // visible = NOT outside (1 or 0 per lane).
    const uint32x4_t v = vmvnq_u32(outside);
    visible[0] = vgetq_lane_u32(v, 0) & 1u;
    visible[1] = vgetq_lane_u32(v, 1) & 1u;
    visible[2] = vgetq_lane_u32(v, 2) & 1u;
    visible[3] = vgetq_lane_u32(v, 3) & 1u;
}
#endif

}  // namespace mge::math
