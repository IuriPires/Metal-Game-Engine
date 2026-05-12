#pragma once

#include "mge/renderer/metal/metal_cpp.h"

namespace mge::renderer::metal {

struct ClearColor {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    double a = 1.0;
};

[[nodiscard]] inline MTL::ClearColor to_mtl(ClearColor c) noexcept {
    return MTL::ClearColor::Make(c.r, c.g, c.b, c.a);
}

}  // namespace mge::renderer::metal
