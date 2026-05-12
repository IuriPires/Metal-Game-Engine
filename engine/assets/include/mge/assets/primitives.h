#pragma once

#include "mge/assets/mesh.h"
#include "mge/math/vec.h"

namespace mge::assets {

// Unit cube centered at origin (side length 2 - vertices at +/-1). 24 unique
// vertices (one per face corner so each face has its own normal). 36 indices.
// Front-face is counter-clockwise when viewed from the outside.
[[nodiscard]] Mesh make_cube(math::Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}) noexcept;

}  // namespace mge::assets
