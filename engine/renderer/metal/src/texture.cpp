#include "mge/rhi/texture.h"

#include "mge/renderer/metal/metal_cpp.h"

namespace mge::rhi {

Texture::~Texture() {
    if (owned_ && native_ != nullptr) {
        static_cast<MTL::Texture*>(native_)->release();
    }
}

}  // namespace mge::rhi
