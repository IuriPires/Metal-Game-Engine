#include "mge/rhi/shader.h"

#include "mge/renderer/metal/metal_cpp.h"

namespace mge::rhi {

Shader::~Shader() {
    if (native_ != nullptr) {
        static_cast<MTL::Library*>(native_)->release();
    }
}

}  // namespace mge::rhi
