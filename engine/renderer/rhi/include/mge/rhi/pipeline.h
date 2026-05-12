#pragma once

#include "mge/rhi/enums.h"
#include "mge/rhi/shader.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace mge::rhi {

struct VertexAttribute {
    std::uint32_t shader_location = 0;   // matches [[attribute(N)]] in MSL
    VertexFormat  format          = VertexFormat::Float32x3;
    std::uint32_t offset          = 0;
    std::uint32_t buffer_slot     = 0;   // index of the vertex buffer slot
};

struct VertexBufferLayout {
    std::uint32_t stride       = 0;
    bool          per_instance = false;
};

struct VertexLayout {
    std::vector<VertexBufferLayout> buffers;
    std::vector<VertexAttribute>    attributes;
};

struct ColorTargetState {
    PixelFormat format    = PixelFormat::BGRA8UnormSrgb;
    bool        blend     = false;
    BlendFactor src_color = BlendFactor::One;
    BlendFactor dst_color = BlendFactor::Zero;
    BlendOp     color_op  = BlendOp::Add;
    BlendFactor src_alpha = BlendFactor::One;
    BlendFactor dst_alpha = BlendFactor::Zero;
    BlendOp     alpha_op  = BlendOp::Add;
};

struct DepthState {
    PixelFormat  format        = PixelFormat::Undefined;
    bool         write_enabled = false;
    DepthCompare compare       = DepthCompare::Less;
};

struct RasterizerState {
    CullMode  cull_mode  = CullMode::Back;
    FrontFace front_face = FrontFace::CounterClockwise;
};

struct RenderPipelineDesc {
    Shader*           vertex_shader = nullptr;
    std::string       vertex_entry   = "vertex_main";
    Shader*           fragment_shader = nullptr;
    std::string       fragment_entry  = "fragment_main";
    VertexLayout      vertex_layout;
    PrimitiveTopology topology  = PrimitiveTopology::TriangleList;
    std::array<ColorTargetState, 8> color_targets{};
    std::uint32_t     num_color_targets = 1;
    DepthState        depth{};
    RasterizerState   rasterizer{};
    std::string       label;
};

class RenderPipeline {
public:
    ~RenderPipeline();
    RenderPipeline(const RenderPipeline&)            = delete;
    RenderPipeline& operator=(const RenderPipeline&) = delete;

    [[nodiscard]] const std::string& label() const noexcept { return label_; }

    [[nodiscard]] void*       native() noexcept { return native_; }
    [[nodiscard]] const void* native() const noexcept { return native_; }

    // Optional Metal MTLDepthStencilState (nullptr if pipeline has no depth).
    [[nodiscard]] void* native_depth_stencil() noexcept { return native_depth_; }

    [[nodiscard]] PrimitiveTopology topology() const noexcept { return topology_; }
    [[nodiscard]] CullMode          cull_mode() const noexcept { return cull_; }
    [[nodiscard]] FrontFace         front_face() const noexcept { return winding_; }

private:
    friend class Device;
    RenderPipeline(void* native, void* depth, PrimitiveTopology t, CullMode c,
                   FrontFace w, std::string label) noexcept
        : native_(native), native_depth_(depth), topology_(t), cull_(c), winding_(w),
          label_(std::move(label)) {}

    void*             native_       = nullptr;
    void*             native_depth_ = nullptr;
    PrimitiveTopology topology_     = PrimitiveTopology::TriangleList;
    CullMode          cull_         = CullMode::Back;
    FrontFace         winding_      = FrontFace::CounterClockwise;
    std::string       label_;
};

struct ComputePipelineDesc {
    Shader*     compute_shader = nullptr;
    std::string compute_entry  = "compute_main";
    std::string label;
};

class ComputePipeline {
public:
    ~ComputePipeline();
    ComputePipeline(const ComputePipeline&)            = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    [[nodiscard]] const std::string& label() const noexcept { return label_; }
    [[nodiscard]] void*              native() noexcept { return native_; }
    [[nodiscard]] const void*        native() const noexcept { return native_; }

    // GPU SIMD width (e.g. 32 on Apple GPU). Useful for picking threadgroup sizes.
    [[nodiscard]] std::uint32_t thread_execution_width() const noexcept { return simd_width_; }

    // Max threads per threadgroup (hard limit from the pipeline).
    [[nodiscard]] std::uint32_t max_total_threads_per_threadgroup() const noexcept {
        return max_tg_;
    }

private:
    friend class Device;
    ComputePipeline(void* native, std::uint32_t simd_width, std::uint32_t max_tg,
                    std::string label) noexcept
        : native_(native), simd_width_(simd_width), max_tg_(max_tg), label_(std::move(label)) {}

    void*         native_     = nullptr;
    std::uint32_t simd_width_ = 0;
    std::uint32_t max_tg_     = 0;
    std::string   label_;
};

}  // namespace mge::rhi
