#include "mge/assets/primitives.h"

namespace mge::assets {

Mesh make_cube(math::Vec4 color) noexcept {
    Mesh m;

    // Six faces, each with 4 vertices + 6 indices, CCW winding outwards.
    struct Face {
        math::Vec3 normal;
        math::Vec3 v[4];  // ordered so triangles {0,1,2}, {0,2,3} are CCW outwards
    };

    constexpr float s = 1.0f;
    const Face faces[6] = {
        // +X
        {{ 1, 0, 0}, {{ s, -s,  s}, { s, -s, -s}, { s,  s, -s}, { s,  s,  s}}},
        // -X
        {{-1, 0, 0}, {{-s, -s, -s}, {-s, -s,  s}, {-s,  s,  s}, {-s,  s, -s}}},
        // +Y
        {{0,  1, 0}, {{-s,  s,  s}, { s,  s,  s}, { s,  s, -s}, {-s,  s, -s}}},
        // -Y
        {{0, -1, 0}, {{-s, -s, -s}, { s, -s, -s}, { s, -s,  s}, {-s, -s,  s}}},
        // +Z
        {{0, 0,  1}, {{-s, -s,  s}, { s, -s,  s}, { s,  s,  s}, {-s,  s,  s}}},
        // -Z
        {{0, 0, -1}, {{ s, -s, -s}, {-s, -s, -s}, {-s,  s, -s}, { s,  s, -s}}},
    };

    m.vertices.reserve(24);
    m.indices.reserve(36);

    for (const auto& f : faces) {
        const std::uint32_t base = static_cast<std::uint32_t>(m.vertices.size());
        for (int i = 0; i < 4; ++i) {
            m.vertices.push_back(LambertVertex{f.v[i], f.normal, color});
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
