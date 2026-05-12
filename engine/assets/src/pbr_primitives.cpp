#include "mge/assets/pbr_mesh.h"
#include "mge/math/constants.h"

#include <cmath>

namespace mge::assets {

PbrMesh make_sphere_pbr(std::uint32_t lat_segs, std::uint32_t lon_segs) noexcept {
    PbrMesh m;
    const std::uint32_t lat_count = lat_segs + 1;   // number of latitude rings
    const std::uint32_t lon_count = lon_segs + 1;   // wrap-around column

    m.vertices.reserve(static_cast<std::size_t>(lat_count) * lon_count);
    for (std::uint32_t y = 0; y < lat_count; ++y) {
        const float v     = static_cast<float>(y) / static_cast<float>(lat_segs);
        const float theta = v * math::pi;
        const float st    = std::sin(theta);
        const float ct    = std::cos(theta);
        for (std::uint32_t x = 0; x < lon_count; ++x) {
            const float u   = static_cast<float>(x) / static_cast<float>(lon_segs);
            const float phi = u * math::two_pi;
            const float sp  = std::sin(phi);
            const float cp  = std::cos(phi);
            const math::Vec3 pos{st * cp, ct, st * sp};
            m.vertices.push_back(PbrVertex{pos, pos});  // normal == pos for unit sphere
        }
    }

    m.indices.reserve(static_cast<std::size_t>(lat_segs) * lon_segs * 6);
    for (std::uint32_t y = 0; y < lat_segs; ++y) {
        for (std::uint32_t x = 0; x < lon_segs; ++x) {
            const std::uint32_t i0 = (y * lon_count) + x;
            const std::uint32_t i1 = i0 + 1;
            const std::uint32_t i2 = i0 + lon_count;
            const std::uint32_t i3 = i2 + 1;
            // CCW outward: (i0, i2, i1) and (i1, i2, i3)
            m.indices.push_back(i0);
            m.indices.push_back(i2);
            m.indices.push_back(i1);
            m.indices.push_back(i1);
            m.indices.push_back(i2);
            m.indices.push_back(i3);
        }
    }
    return m;
}

PbrMesh make_ground_plane_pbr(float half_extent) noexcept {
    PbrMesh m;
    const math::Vec3 n{0, 1, 0};
    m.vertices = {
        PbrVertex{math::Vec3{-half_extent, 0, -half_extent}, n},
        PbrVertex{math::Vec3{ half_extent, 0, -half_extent}, n},
        PbrVertex{math::Vec3{ half_extent, 0,  half_extent}, n},
        PbrVertex{math::Vec3{-half_extent, 0,  half_extent}, n},
    };
    // CCW outward (camera above looks down at +Y plane).
    m.indices = {0, 2, 1, 0, 3, 2};
    return m;
}

PbrMesh make_cube_pbr() noexcept {
    PbrMesh m;
    struct Face {
        math::Vec3 normal;
        math::Vec3 v[4];
    };
    constexpr float s = 1.0f;
    const Face faces[6] = {
        {{ 1, 0, 0}, {{ s, -s,  s}, { s, -s, -s}, { s,  s, -s}, { s,  s,  s}}},
        {{-1, 0, 0}, {{-s, -s, -s}, {-s, -s,  s}, {-s,  s,  s}, {-s,  s, -s}}},
        {{0,  1, 0}, {{-s,  s,  s}, { s,  s,  s}, { s,  s, -s}, {-s,  s, -s}}},
        {{0, -1, 0}, {{-s, -s, -s}, { s, -s, -s}, { s, -s,  s}, {-s, -s,  s}}},
        {{0, 0,  1}, {{-s, -s,  s}, { s, -s,  s}, { s,  s,  s}, {-s,  s,  s}}},
        {{0, 0, -1}, {{ s, -s, -s}, {-s, -s, -s}, {-s,  s, -s}, { s,  s, -s}}},
    };
    m.vertices.reserve(24);
    m.indices.reserve(36);
    for (const auto& f : faces) {
        const std::uint32_t base = static_cast<std::uint32_t>(m.vertices.size());
        for (int i = 0; i < 4; ++i) {
            m.vertices.push_back(PbrVertex{f.v[i], f.normal});
        }
        m.indices.push_back(base + 0);
        m.indices.push_back(base + 1);
        m.indices.push_back(base + 2);
        m.indices.push_back(base + 0);
        m.indices.push_back(base + 2);
        m.indices.push_back(base + 3);
    }
    return m;
}

}  // namespace mge::assets
