#pragma once

#include "mge/rhi/enums.h"
#include "mge/rhi/pipeline.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace mge::rhi {

class Buffer;
class Texture;
class Sampler;

struct ColorAttachment {
    Texture*    texture        = nullptr;     // owned by caller; not retained
    LoadAction  load_action    = LoadAction::Clear;
    StoreAction store_action   = StoreAction::Store;
    float       clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct DepthAttachment {
    Texture*    texture      = nullptr;
    LoadAction  load_action  = LoadAction::Clear;
    StoreAction store_action = StoreAction::DontCare;
    float       clear_depth  = 1.0f;
};

struct RenderPassDesc {
    std::array<ColorAttachment, 8> color_attachments{};
    std::uint32_t                  num_color_attachments = 1;
    DepthAttachment                depth_attachment{};
    bool                           has_depth = false;
    std::string                    label;
};

struct Viewport {
    float x        = 0;
    float y        = 0;
    float width    = 0;
    float height   = 0;
    float min_depth = 0;
    float max_depth = 1;
};

// RAII render encoder. end() is called by destructor if not called manually.
// Re-entrant rendering is disallowed - the parent command buffer cannot be
// used until the encoder is destroyed.
class RenderEncoder {
public:
    ~RenderEncoder();
    RenderEncoder(const RenderEncoder&)            = delete;
    RenderEncoder& operator=(const RenderEncoder&) = delete;
    RenderEncoder(RenderEncoder&&) noexcept;
    RenderEncoder& operator=(RenderEncoder&&) = delete;

    void set_pipeline(RenderPipeline& pipeline);
    void set_vertex_buffer(Buffer& buffer, std::uint32_t slot, std::size_t offset = 0);
    void set_fragment_buffer(Buffer& buffer, std::uint32_t slot, std::size_t offset = 0);
    void set_fragment_texture(Texture& texture, std::uint32_t slot);
    void set_fragment_sampler(Sampler& sampler, std::uint32_t slot);
    void set_viewport(Viewport vp);

    void draw(std::uint32_t vertex_count, std::uint32_t instance_count = 1,
              std::uint32_t base_vertex = 0, std::uint32_t base_instance = 0);

    void draw_indexed(std::uint32_t index_count, IndexType index_type, Buffer& index_buffer,
                      std::size_t index_offset = 0, std::uint32_t instance_count = 1,
                      std::int32_t base_vertex = 0, std::uint32_t base_instance = 0);

    // Explicit end - or just let the destructor do it.
    void end() noexcept;

    [[nodiscard]] void* native() noexcept { return native_; }

private:
    friend class CommandBuffer;
    RenderEncoder(void* native) noexcept : native_(native) {}
    void*             native_   = nullptr;
    PrimitiveTopology topology_ = PrimitiveTopology::TriangleList;
};

}  // namespace mge::rhi
