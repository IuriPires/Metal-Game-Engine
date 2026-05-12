#include "mge/rhi/device.h"

#include "format_conv.h"
#include "mge/renderer/metal/metal_cpp.h"

#include <cstdio>
#include <cstring>
#include <utility>

namespace mge::rhi {

namespace mb = metal_backend;

std::unique_ptr<Device> Device::create() {
    MTL::Device* dev = MTL::CreateSystemDefaultDevice();
    if (dev == nullptr) {
        return nullptr;
    }
    std::unique_ptr<Device> result(new Device());
    result->native_ = dev;
    return result;
}

Device::~Device() {
    if (native_ != nullptr) {
        static_cast<MTL::Device*>(native_)->release();
    }
}

DeviceInfo Device::info() const {
    DeviceInfo i;
    auto*      dev = static_cast<const MTL::Device*>(native_);
    if (dev == nullptr) {
        return i;
    }
    NS::String* name = const_cast<MTL::Device*>(dev)->name();
    if (name != nullptr) {
        i.name = name->utf8String();
    }
    i.has_unified_memory   = const_cast<MTL::Device*>(dev)->hasUnifiedMemory();
    i.supports_ray_tracing             = const_cast<MTL::Device*>(dev)->supportsRaytracing();
    i.supports_ray_tracing_from_render =
        const_cast<MTL::Device*>(dev)->supportsRaytracingFromRender();
    i.low_power            = const_cast<MTL::Device*>(dev)->lowPower();
    return i;
}

std::unique_ptr<Queue> Device::create_queue(std::string label) {
    auto* dev = static_cast<MTL::Device*>(native_);
    MTL::CommandQueue* q = dev->newCommandQueue();
    if (q == nullptr) {
        return nullptr;
    }
    if (!label.empty()) {
        q->setLabel(mb::ns_str(label));
    }
    return std::unique_ptr<Queue>(new Queue(q, std::move(label)));
}

std::unique_ptr<Buffer> Device::create_buffer(const BufferDesc& desc) {
    auto* dev = static_cast<MTL::Device*>(native_);
    if (desc.size == 0) {
        return nullptr;
    }

    MTL::ResourceOptions opts = mb::resource_options_for(desc.storage);
    MTL::Buffer*         buf  = nullptr;

    if (desc.initial_data != nullptr && desc.initial_data_size > 0 &&
        desc.storage != StorageMode::Private) {
        buf = dev->newBuffer(desc.initial_data, desc.size, opts);
    } else {
        buf = dev->newBuffer(desc.size, opts);
        if (buf != nullptr && desc.initial_data != nullptr && desc.initial_data_size > 0 &&
            desc.storage != StorageMode::Private) {
            const std::size_t n =
                desc.initial_data_size < desc.size ? desc.initial_data_size : desc.size;
            std::memcpy(buf->contents(), desc.initial_data, n);
        }
    }
    if (buf == nullptr) {
        return nullptr;
    }
    if (!desc.label.empty()) {
        buf->setLabel(mb::ns_str(desc.label));
    }
    return std::unique_ptr<Buffer>(new Buffer(buf, desc.size, desc.usage, desc.storage,
                                              desc.label));
}

std::unique_ptr<Texture> Device::create_texture(const TextureDesc& desc) {
    auto* dev = static_cast<MTL::Device*>(native_);

    MTL::TextureDescriptor* td = MTL::TextureDescriptor::alloc()->init();
    td->setTextureType(MTL::TextureType2D);
    td->setPixelFormat(mb::to_mtl(desc.format));
    td->setWidth(desc.width);
    td->setHeight(desc.height);
    td->setDepth(desc.depth);
    td->setMipmapLevelCount(desc.mip_levels);
    td->setStorageMode(mb::to_mtl(desc.storage));
    td->setUsage(mb::to_mtl(desc.usage));

    MTL::Texture* tex = dev->newTexture(td);
    td->release();
    if (tex == nullptr) {
        return nullptr;
    }
    if (!desc.label.empty()) {
        tex->setLabel(mb::ns_str(desc.label));
    }
    return std::unique_ptr<Texture>(new Texture(tex, desc, /*owned=*/true));
}

std::unique_ptr<Sampler> Device::create_sampler(const SamplerDesc& desc) {
    auto* dev = static_cast<MTL::Device*>(native_);

    MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
    sd->setMinFilter(mb::to_mtl(desc.min_filter));
    sd->setMagFilter(mb::to_mtl(desc.mag_filter));
    sd->setMipFilter(mb::to_mtl(desc.mip_filter));
    sd->setSAddressMode(mb::to_mtl(desc.address_u));
    sd->setTAddressMode(mb::to_mtl(desc.address_v));
    sd->setRAddressMode(mb::to_mtl(desc.address_w));
    sd->setMaxAnisotropy(desc.max_anisotropy);
    if (!desc.label.empty()) {
        sd->setLabel(mb::ns_str(desc.label));
    }

    MTL::SamplerState* state = dev->newSamplerState(sd);
    sd->release();
    if (state == nullptr) {
        return nullptr;
    }
    return std::unique_ptr<Sampler>(new Sampler(state, desc.label));
}

std::unique_ptr<Shader> Device::create_shader_from_msl(const ShaderSourceDesc& desc) {
    auto* dev = static_cast<MTL::Device*>(native_);

    NS::String* src = NS::String::string(desc.msl_source.data(),
                                          NS::UTF8StringEncoding);
    MTL::CompileOptions* opts = MTL::CompileOptions::alloc()->init();
    NS::Error*           err  = nullptr;

    MTL::Library* lib = dev->newLibrary(src, opts, &err);
    opts->release();
    if (lib == nullptr) {
        if (err != nullptr) {
            const char* msg = err->localizedDescription()->utf8String();
            std::fprintf(stderr, "[rhi/metal] shader compile failed: %s\n",
                         msg ? msg : "(null)");
        }
        return nullptr;
    }
    if (!desc.label.empty()) {
        lib->setLabel(mb::ns_str(desc.label));
    }
    return std::unique_ptr<Shader>(new Shader(lib, desc.label));
}

std::unique_ptr<RenderPipeline> Device::create_render_pipeline(const RenderPipelineDesc& desc) {
    auto* dev = static_cast<MTL::Device*>(native_);

    if (desc.vertex_shader == nullptr) {
        return nullptr;
    }

    auto* vlib = static_cast<MTL::Library*>(desc.vertex_shader->native());
    MTL::Function* vfn = vlib->newFunction(mb::ns_str(desc.vertex_entry));
    if (vfn == nullptr) {
        std::fprintf(stderr, "[rhi/metal] missing vertex function: %s\n",
                     desc.vertex_entry.c_str());
        return nullptr;
    }

    // Depth-only passes (e.g. shadow mapping) skip the fragment stage.
    MTL::Function* ffn = nullptr;
    if (desc.fragment_shader != nullptr) {
        auto* flib = static_cast<MTL::Library*>(desc.fragment_shader->native());
        ffn = flib->newFunction(mb::ns_str(desc.fragment_entry));
        if (ffn == nullptr) {
            vfn->release();
            std::fprintf(stderr, "[rhi/metal] missing fragment function: %s\n",
                         desc.fragment_entry.c_str());
            return nullptr;
        }
    }

    MTL::RenderPipelineDescriptor* pd = MTL::RenderPipelineDescriptor::alloc()->init();
    pd->setVertexFunction(vfn);
    if (ffn != nullptr) {
        pd->setFragmentFunction(ffn);
    }

    for (std::uint32_t i = 0; i < desc.num_color_targets; ++i) {
        const auto& ct = desc.color_targets[i];
        MTL::RenderPipelineColorAttachmentDescriptor* ca =
            pd->colorAttachments()->object(i);
        ca->setPixelFormat(mb::to_mtl(ct.format));
        ca->setBlendingEnabled(ct.blend);
        if (ct.blend) {
            ca->setSourceRGBBlendFactor(mb::to_mtl(ct.src_color));
            ca->setDestinationRGBBlendFactor(mb::to_mtl(ct.dst_color));
            ca->setRgbBlendOperation(mb::to_mtl(ct.color_op));
            ca->setSourceAlphaBlendFactor(mb::to_mtl(ct.src_alpha));
            ca->setDestinationAlphaBlendFactor(mb::to_mtl(ct.dst_alpha));
            ca->setAlphaBlendOperation(mb::to_mtl(ct.alpha_op));
        }
    }
    if (desc.depth.format != PixelFormat::Undefined) {
        pd->setDepthAttachmentPixelFormat(mb::to_mtl(desc.depth.format));
    }

    // Vertex descriptor.
    if (!desc.vertex_layout.buffers.empty() || !desc.vertex_layout.attributes.empty()) {
        MTL::VertexDescriptor* vd = MTL::VertexDescriptor::alloc()->init();
        for (const auto& a : desc.vertex_layout.attributes) {
            MTL::VertexAttributeDescriptor* ad = vd->attributes()->object(a.shader_location);
            ad->setFormat(mb::to_mtl(a.format));
            ad->setOffset(a.offset);
            ad->setBufferIndex(a.buffer_slot);
        }
        for (std::size_t i = 0; i < desc.vertex_layout.buffers.size(); ++i) {
            const auto& b = desc.vertex_layout.buffers[i];
            MTL::VertexBufferLayoutDescriptor* bd = vd->layouts()->object(i);
            bd->setStride(b.stride);
            bd->setStepFunction(b.per_instance ? MTL::VertexStepFunctionPerInstance
                                                : MTL::VertexStepFunctionPerVertex);
            bd->setStepRate(1);
        }
        pd->setVertexDescriptor(vd);
        vd->release();
    }

    if (!desc.label.empty()) {
        pd->setLabel(mb::ns_str(desc.label));
    }

    NS::Error*               err = nullptr;
    MTL::RenderPipelineState* pso = dev->newRenderPipelineState(pd, &err);
    pd->release();
    vfn->release();
    if (ffn != nullptr) {
        ffn->release();
    }

    if (pso == nullptr) {
        if (err != nullptr) {
            std::fprintf(stderr, "[rhi/metal] pipeline build failed: %s\n",
                         err->localizedDescription()->utf8String());
        }
        return nullptr;
    }

    // Depth-stencil state is a separate Metal object - build it from the desc
    // and stash alongside the pipeline so the encoder can bind both when the
    // pipeline is set.
    MTL::DepthStencilState* dss = nullptr;
    if (desc.depth.format != PixelFormat::Undefined) {
        MTL::DepthStencilDescriptor* dsd = MTL::DepthStencilDescriptor::alloc()->init();
        dsd->setDepthCompareFunction(mb::to_mtl(desc.depth.compare));
        dsd->setDepthWriteEnabled(desc.depth.write_enabled);
        dss = dev->newDepthStencilState(dsd);
        dsd->release();
    }

    return std::unique_ptr<RenderPipeline>(new RenderPipeline(
        pso, dss, desc.topology, desc.rasterizer.cull_mode,
        desc.rasterizer.front_face, desc.label));
}

std::unique_ptr<ComputePipeline> Device::create_compute_pipeline(const ComputePipelineDesc& desc) {
    if (desc.compute_shader == nullptr) {
        std::fprintf(stderr, "[rhi/metal] compute pipeline missing shader\n");
        return nullptr;
    }
    auto* dev  = static_cast<MTL::Device*>(native_);
    auto* clib = static_cast<MTL::Library*>(desc.compute_shader->native());
    MTL::Function* cfn = clib->newFunction(mb::ns_str(desc.compute_entry));
    if (cfn == nullptr) {
        std::fprintf(stderr, "[rhi/metal] missing compute function: %s\n",
                     desc.compute_entry.c_str());
        return nullptr;
    }

    NS::Error* err = nullptr;
    MTL::ComputePipelineState* pso =
        dev->newComputePipelineState(cfn, &err);
    cfn->release();

    if (pso == nullptr) {
        if (err != nullptr) {
            std::fprintf(stderr, "[rhi/metal] compute pipeline build failed: %s\n",
                         err->localizedDescription()->utf8String());
        }
        return nullptr;
    }

    const auto simd_w = static_cast<std::uint32_t>(pso->threadExecutionWidth());
    const auto max_tg = static_cast<std::uint32_t>(pso->maxTotalThreadsPerThreadgroup());

    return std::unique_ptr<ComputePipeline>(
        new ComputePipeline(pso, simd_w, max_tg, desc.label));
}

std::unique_ptr<Swapchain> Device::create_swapchain(void* native_layer, PixelFormat fmt) {
    if (native_layer == nullptr) {
        return nullptr;
    }
    auto* layer = static_cast<CA::MetalLayer*>(native_layer);
    layer->setDevice(static_cast<MTL::Device*>(native_));
    layer->setPixelFormat(mb::to_mtl(fmt));
    layer->setFramebufferOnly(true);
    return std::unique_ptr<Swapchain>(new Swapchain(layer, this, fmt));
}

}  // namespace mge::rhi
