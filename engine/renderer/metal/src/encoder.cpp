#include "mge/rhi/encoder.h"

#include "mge/rhi/buffer.h"
#include "mge/rhi/pipeline.h"

#include "format_conv.h"
#include "mge/renderer/metal/metal_cpp.h"

namespace mge::rhi {

namespace mb = metal_backend;

RenderEncoder::~RenderEncoder() {
    end();
}

RenderEncoder::RenderEncoder(RenderEncoder&& other) noexcept
    : native_(other.native_), topology_(other.topology_) {
    other.native_ = nullptr;
}

void RenderEncoder::set_pipeline(RenderPipeline& pipeline) {
    auto* enc = static_cast<MTL::RenderCommandEncoder*>(native_);
    enc->setRenderPipelineState(
        static_cast<MTL::RenderPipelineState*>(pipeline.native()));

    if (void* dss = pipeline.native_depth_stencil(); dss != nullptr) {
        enc->setDepthStencilState(static_cast<MTL::DepthStencilState*>(dss));
    }
    enc->setCullMode(mb::to_mtl(pipeline.cull_mode()));
    enc->setFrontFacingWinding(mb::to_mtl(pipeline.front_face()));

    topology_ = pipeline.topology();
}

void RenderEncoder::set_vertex_buffer(Buffer& buffer, std::uint32_t slot,
                                       std::size_t offset) {
    auto* enc = static_cast<MTL::RenderCommandEncoder*>(native_);
    enc->setVertexBuffer(static_cast<MTL::Buffer*>(buffer.native()), offset, slot);
}

void RenderEncoder::set_fragment_buffer(Buffer& buffer, std::uint32_t slot,
                                         std::size_t offset) {
    auto* enc = static_cast<MTL::RenderCommandEncoder*>(native_);
    enc->setFragmentBuffer(static_cast<MTL::Buffer*>(buffer.native()), offset, slot);
}

void RenderEncoder::set_viewport(Viewport vp) {
    auto* enc = static_cast<MTL::RenderCommandEncoder*>(native_);
    MTL::Viewport v{};
    v.originX = static_cast<double>(vp.x);
    v.originY = static_cast<double>(vp.y);
    v.width   = static_cast<double>(vp.width);
    v.height  = static_cast<double>(vp.height);
    v.znear   = static_cast<double>(vp.min_depth);
    v.zfar    = static_cast<double>(vp.max_depth);
    enc->setViewport(v);
}

void RenderEncoder::draw(std::uint32_t vertex_count, std::uint32_t instance_count,
                          std::uint32_t base_vertex, std::uint32_t base_instance) {
    auto* enc = static_cast<MTL::RenderCommandEncoder*>(native_);
    enc->drawPrimitives(mb::to_mtl(topology_), base_vertex, vertex_count,
                         instance_count, base_instance);
}

void RenderEncoder::draw_indexed(std::uint32_t index_count, IndexType index_type,
                                  Buffer& index_buffer, std::size_t index_offset,
                                  std::uint32_t instance_count, std::int32_t base_vertex,
                                  std::uint32_t base_instance) {
    auto* enc = static_cast<MTL::RenderCommandEncoder*>(native_);
    enc->drawIndexedPrimitives(
        mb::to_mtl(topology_),
        index_count,
        mb::to_mtl(index_type),
        static_cast<MTL::Buffer*>(index_buffer.native()),
        index_offset,
        instance_count,
        base_vertex,
        base_instance);
}

void RenderEncoder::end() noexcept {
    if (native_ != nullptr) {
        static_cast<MTL::RenderCommandEncoder*>(native_)->endEncoding();
        native_ = nullptr;
    }
}

}  // namespace mge::rhi
