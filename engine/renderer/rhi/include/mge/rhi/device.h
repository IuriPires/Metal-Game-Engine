#pragma once

#include "mge/rhi/acceleration_structure.h"
#include "mge/rhi/buffer.h"
#include "mge/rhi/pipeline.h"
#include "mge/rhi/queue.h"
#include "mge/rhi/sampler.h"
#include "mge/rhi/shader.h"
#include "mge/rhi/swapchain.h"
#include "mge/rhi/texture.h"

#include <memory>
#include <string>

namespace mge::rhi {

struct DeviceInfo {
    std::string name;
    bool        has_unified_memory          = false;
    bool        supports_ray_tracing        = false;
    bool        supports_ray_tracing_from_render = false;  // ray queries in vert/frag
    bool        low_power                   = false;
};

class Device {
public:
    // Acquire the system default device. Returns nullptr if no Metal-capable
    // GPU is available.
    [[nodiscard]] static std::unique_ptr<Device> create();

    ~Device();
    Device(const Device&)            = delete;
    Device& operator=(const Device&) = delete;

    [[nodiscard]] DeviceInfo info() const;

    [[nodiscard]] std::unique_ptr<Queue>          create_queue(std::string label = "default");
    [[nodiscard]] std::unique_ptr<Buffer>         create_buffer(const BufferDesc& desc);
    [[nodiscard]] std::unique_ptr<Texture>        create_texture(const TextureDesc& desc);
    [[nodiscard]] std::unique_ptr<Sampler>        create_sampler(const SamplerDesc& desc);
    [[nodiscard]] std::unique_ptr<Shader>         create_shader_from_msl(const ShaderSourceDesc& desc);
    [[nodiscard]] std::unique_ptr<RenderPipeline>  create_render_pipeline(const RenderPipelineDesc& desc);
    [[nodiscard]] std::unique_ptr<ComputePipeline> create_compute_pipeline(const ComputePipelineDesc& desc);

    // Build a bottom-level (primitive) acceleration structure. Blocking — the
    // caller's Queue is used for the build, and the call returns only after
    // the GPU has finished. v1 doesn't support refit/rebuild.
    [[nodiscard]] std::unique_ptr<AccelerationStructure>
        build_acceleration_structure(Queue& queue, const PrimitiveAccelDesc& desc);

    // Build a top-level (instance) acceleration structure. Same semantics as
    // the primitive variant.
    [[nodiscard]] std::unique_ptr<AccelerationStructure>
        build_acceleration_structure(Queue& queue, const InstanceAccelDesc& desc);

    // native_layer: void* CAMetalLayer pointer obtained from mge::platform::Window::native_layer().
    [[nodiscard]] std::unique_ptr<Swapchain> create_swapchain(void* native_layer,
                                                              PixelFormat fmt = PixelFormat::BGRA8UnormSrgb);

    [[nodiscard]] void* native() noexcept { return native_; }

private:
    Device() = default;
    void* native_ = nullptr;
};

}  // namespace mge::rhi
