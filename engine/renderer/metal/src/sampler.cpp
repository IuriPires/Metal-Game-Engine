#include "mge/rhi/sampler.h"

#include "mge/renderer/metal/metal_cpp.h"

namespace mge::rhi {

Sampler::~Sampler() {
    if (native_ != nullptr) {
        static_cast<MTL::SamplerState*>(native_)->release();
    }
}

}  // namespace mge::rhi
