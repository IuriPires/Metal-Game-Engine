#pragma once

#include "mge/math/vec.h"

#include <cstdint>
#include <vector>

namespace mge::assets {

// CPU-side vertex layout used by the M4 Lambert pipeline. 40 bytes/vertex.
// Matches the inline MSL shader's VSIn.
struct LambertVertex {
    math::Vec3 position;  // 16 B (Vec3 is padded to 16 for ABI parity)
    math::Vec3 normal;    // 16 B
    math::Vec4 color;     // 16 B
};

// Note on size: Vec3 is 16B-padded by ABI choice (ADR-0003). LambertVertex
// is then 48 bytes total. Stride matches RHI VertexBufferLayout::stride.
static_assert(sizeof(LambertVertex) == 48);

struct Mesh {
    std::vector<LambertVertex> vertices;
    std::vector<std::uint32_t> indices;
};

}  // namespace mge::assets
