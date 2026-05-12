#include "mge/rhi/acceleration_structure.h"
#include "mge/rhi/buffer.h"
#include "mge/rhi/command_buffer.h"
#include "mge/rhi/device.h"
#include "mge/rhi/queue.h"

#include "format_conv.h"
#include "mge/renderer/metal/metal_cpp.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace mge::rhi {

namespace mb = metal_backend;

namespace {

[[nodiscard]] MTL::AttributeFormat to_attribute_format(VertexFormat f) noexcept {
    switch (f) {
        case VertexFormat::Float32:   return MTL::AttributeFormatFloat;
        case VertexFormat::Float32x2: return MTL::AttributeFormatFloat2;
        case VertexFormat::Float32x3: return MTL::AttributeFormatFloat3;
        case VertexFormat::Float32x4: return MTL::AttributeFormatFloat4;
        default: break;
    }
    // BVH builds require positional float data; other formats aren't expected.
    return MTL::AttributeFormatFloat3;
}

// Build any AS (primitive or instance). The caller owns the descriptor and
// passes a label hint. Returns the new AccelerationStructure or nullptr.
[[nodiscard]] std::unique_ptr<AccelerationStructure> build_blocking(
    Device& device,
    Queue&  queue,
    MTL::AccelerationStructureDescriptor* desc,
    bool                                  is_instance,
    std::string                           label) {
    auto* dev = static_cast<MTL::Device*>(device.native());

    const MTL::AccelerationStructureSizes sizes = dev->accelerationStructureSizes(desc);

    MTL::AccelerationStructure* as =
        dev->newAccelerationStructure(sizes.accelerationStructureSize);
    if (as == nullptr) {
        std::fprintf(stderr, "[rhi/metal] newAccelerationStructure(%llu) failed\n",
                     static_cast<unsigned long long>(sizes.accelerationStructureSize));
        return nullptr;
    }
    if (!label.empty()) {
        as->setLabel(mb::ns_str(label));
    }

    MTL::Buffer* scratch =
        dev->newBuffer(sizes.buildScratchBufferSize, MTL::ResourceStorageModePrivate);
    if (scratch == nullptr) {
        as->release();
        std::fprintf(stderr, "[rhi/metal] AS scratch buffer alloc failed\n");
        return nullptr;
    }

    {
        CommandBuffer cmd = queue.create_command_buffer();
        auto* mtl_cmd     = static_cast<MTL::CommandBuffer*>(cmd.native());

        MTL::AccelerationStructureCommandEncoder* enc =
            mtl_cmd->accelerationStructureCommandEncoder();
        enc->buildAccelerationStructure(as, desc, scratch, 0);
        enc->endEncoding();

        cmd.commit();
        cmd.wait_until_completed();
    }

    scratch->release();
    return std::unique_ptr<AccelerationStructure>(
        new AccelerationStructure(as, is_instance, std::move(label)));
}

}  // namespace

AccelerationStructure::~AccelerationStructure() {
    if (native_ != nullptr) {
        static_cast<MTL::AccelerationStructure*>(native_)->release();
    }
}

std::unique_ptr<AccelerationStructure>
Device::build_acceleration_structure(Queue& queue, const PrimitiveAccelDesc& desc) {
    if (desc.geometries.empty()) {
        std::fprintf(stderr, "[rhi/metal] primitive AS with zero geometries\n");
        return nullptr;
    }

    NS::Array* geom_array = nullptr;
    std::vector<MTL::AccelerationStructureTriangleGeometryDescriptor*> geoms;
    geoms.reserve(desc.geometries.size());

    for (const auto& g : desc.geometries) {
        auto* tg = MTL::AccelerationStructureTriangleGeometryDescriptor::alloc()->init();
        if (g.vertex_buffer != nullptr) {
            tg->setVertexBuffer(static_cast<MTL::Buffer*>(g.vertex_buffer->native()));
            tg->setVertexBufferOffset(g.vertex_offset);
        }
        tg->setVertexStride(g.vertex_stride);
        tg->setVertexFormat(to_attribute_format(g.vertex_format));
        if (g.index_buffer != nullptr) {
            tg->setIndexBuffer(static_cast<MTL::Buffer*>(g.index_buffer->native()));
            tg->setIndexBufferOffset(g.index_offset);
            tg->setIndexType(mb::to_mtl(g.index_type));
        }
        tg->setTriangleCount(g.triangle_count);
        tg->setOpaque(g.opaque);
        geoms.push_back(tg);
    }

    geom_array = NS::Array::array(reinterpret_cast<const NS::Object* const*>(geoms.data()),
                                   geoms.size());

    auto* primitive_desc = MTL::PrimitiveAccelerationStructureDescriptor::alloc()->init();
    primitive_desc->setGeometryDescriptors(geom_array);

    auto result = build_blocking(*this, queue, primitive_desc, false,
                                  desc.label.empty() ? "blas" : desc.label);

    primitive_desc->release();
    for (auto* g : geoms) g->release();
    return result;
}

std::unique_ptr<AccelerationStructure>
Device::build_acceleration_structure(Queue& queue, const InstanceAccelDesc& desc) {
    if (desc.instances.empty() || desc.blas.empty()) {
        std::fprintf(stderr, "[rhi/metal] instance AS missing instances/blas\n");
        return nullptr;
    }

    auto* dev = static_cast<MTL::Device*>(native_);

    // Pack one MTL::AccelerationStructureInstanceDescriptor per instance into a
    // Shared buffer the GPU reads during the build.
    const std::size_t inst_stride = sizeof(MTL::AccelerationStructureInstanceDescriptor);
    const std::size_t inst_bytes  = desc.instances.size() * inst_stride;

    MTL::Buffer* inst_buf =
        dev->newBuffer(inst_bytes, MTL::ResourceStorageModeShared);
    if (inst_buf == nullptr) {
        std::fprintf(stderr, "[rhi/metal] AS instance buffer alloc failed\n");
        return nullptr;
    }

    auto* dst = static_cast<MTL::AccelerationStructureInstanceDescriptor*>(inst_buf->contents());
    for (std::size_t i = 0; i < desc.instances.size(); ++i) {
        const auto& in = desc.instances[i];
        auto&       od = dst[i];
        // PackedFloat4x3 is column-major: 4 columns of float3. Our transform is
        // row-major 3x4 [m00 m01 m02 m03 / m10 m11 m12 m13 / m20 m21 m22 m23].
        // Column k (k=0..3) is (m0k, m1k, m2k).
        od.transformationMatrix.columns[0] = {in.transform_3x4[0], in.transform_3x4[4],
                                              in.transform_3x4[8]};
        od.transformationMatrix.columns[1] = {in.transform_3x4[1], in.transform_3x4[5],
                                              in.transform_3x4[9]};
        od.transformationMatrix.columns[2] = {in.transform_3x4[2], in.transform_3x4[6],
                                              in.transform_3x4[10]};
        od.transformationMatrix.columns[3] = {in.transform_3x4[3], in.transform_3x4[7],
                                              in.transform_3x4[11]};
        MTL::AccelerationStructureInstanceOptions opt =
            MTL::AccelerationStructureInstanceOptionNone;
        if (in.opaque) opt |= MTL::AccelerationStructureInstanceOptionOpaque;
        od.options                          = opt;
        od.mask                             = in.mask;
        od.intersectionFunctionTableOffset  = 0;
        od.accelerationStructureIndex       = in.blas_index;
    }

    // Build an NS::Array of the BLAS pointers (mge::rhi → MTL).
    std::vector<MTL::AccelerationStructure*> blas_natives;
    blas_natives.reserve(desc.blas.size());
    for (auto* b : desc.blas) {
        blas_natives.push_back(static_cast<MTL::AccelerationStructure*>(b->native()));
    }
    NS::Array* blas_array = NS::Array::array(
        reinterpret_cast<const NS::Object* const*>(blas_natives.data()),
        blas_natives.size());

    auto* instance_desc = MTL::InstanceAccelerationStructureDescriptor::alloc()->init();
    instance_desc->setInstanceCount(desc.instances.size());
    instance_desc->setInstanceDescriptorBuffer(inst_buf);
    instance_desc->setInstanceDescriptorBufferOffset(0);
    instance_desc->setInstanceDescriptorStride(inst_stride);
    instance_desc->setInstanceDescriptorType(
        MTL::AccelerationStructureInstanceDescriptorTypeDefault);
    instance_desc->setInstancedAccelerationStructures(blas_array);

    auto result = build_blocking(*this, queue, instance_desc, true,
                                  desc.label.empty() ? "tlas" : desc.label);

    instance_desc->release();
    inst_buf->release();
    return result;
}

}  // namespace mge::rhi
