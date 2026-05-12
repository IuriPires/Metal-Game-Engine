#pragma once

#include "mge/math/vec.h"

#include <cstdint>
#include <vector>

namespace mge::assets {

// 32-byte vertex used by the M6 deferred PBR path. Position + world-space
// normal (no per-vertex color - material drives albedo/metallic/roughness).
// Vec3 is 16-byte padded in our ABI, so the struct lays out cleanly with
// position at offset 0 and normal at offset 16. Stride is 32 B.
struct PbrVertex {
    math::Vec3 position;  // 16 B
    math::Vec3 normal;    // 16 B
};

static_assert(sizeof(PbrVertex) == 32);

struct PbrMesh {
    std::vector<PbrVertex>     vertices;
    std::vector<std::uint32_t> indices;
};

// Unit sphere centered at origin. `lat_segs` rings between poles (excludes the
// pole rings themselves), `lon_segs` slices around the equator. Default 16x32
// gives 510 vertices, 960 triangles - smooth-shaded ball at a reasonable cost.
// Front face is CCW when viewed from outside.
[[nodiscard]] PbrMesh make_sphere_pbr(std::uint32_t lat_segs = 16,
                                       std::uint32_t lon_segs = 32) noexcept;

// Unit cube with axis-aligned normals; useful as a PBR test mesh that doesn't
// have the smoothing artifacts of low-poly spheres.
[[nodiscard]] PbrMesh make_cube_pbr() noexcept;

// Flat quad in the XZ plane at y=0, with normal=+Y. `half_extent` is the
// distance from origin to each edge along X and Z. Used as a ground plane
// for shadow tests.
[[nodiscard]] PbrMesh make_ground_plane_pbr(float half_extent = 10.0f) noexcept;

}  // namespace mge::assets
