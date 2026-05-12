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
    // Blend factors / op intentionally omitted for M3; add when transparency lands (M9).
};

struct DepthState {
    PixelFormat format        = PixelFormat::Undefined;
    bool        write_enabled = false;
    // Depth compare: M5+
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

    // Cached topology - render encoders need it at draw time, separate from PSO.
    [[nodiscard]] PrimitiveTopology topology() const noexcept { return topology_; }

private:
    friend class Device;
    RenderPipeline(void* native, PrimitiveTopology t, std::string label) noexcept
        : native_(native), topology_(t), label_(std::move(label)) {}

    void*             native_   = nullptr;
    PrimitiveTopology topology_ = PrimitiveTopology::TriangleList;
    std::string       label_;
};

}  // namespace mge::rhi
