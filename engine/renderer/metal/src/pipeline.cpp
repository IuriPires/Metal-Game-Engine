#include "mge/rhi/pipeline.h"

#include "mge/renderer/metal/metal_cpp.h"

namespace mge::rhi {

RenderPipeline::~RenderPipeline() {
    if (native_ != nullptr) {
        static_cast<MTL::RenderPipelineState*>(native_)->release();
    }
    if (native_depth_ != nullptr) {
        static_cast<MTL::DepthStencilState*>(native_depth_)->release();
    }
}

ComputePipeline::~ComputePipeline() {
    if (native_ != nullptr) {
        static_cast<MTL::ComputePipelineState*>(native_)->release();
    }
}

}  // namespace mge::rhi
