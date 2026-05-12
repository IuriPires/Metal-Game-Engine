#include "mge/rhi/pipeline.h"

#include "mge/renderer/metal/metal_cpp.h"

namespace mge::rhi {

RenderPipeline::~RenderPipeline() {
    if (native_ != nullptr) {
        static_cast<MTL::RenderPipelineState*>(native_)->release();
    }
}

}  // namespace mge::rhi
