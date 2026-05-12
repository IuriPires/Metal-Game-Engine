#pragma once

#include "mge/frame_graph/handles.h"
#include "mge/rhi/encoder.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mge::rhi {
class CommandBuffer;
class Device;
class Texture;
}  // namespace mge::rhi

namespace mge::frame_graph {

class FrameGraph;

// Per-pass resource binding (handle + how the pass uses it). Pass builders
// emit these; the graph reads them during compile() to derive lifetimes and
// attachments.
struct ResourceBinding {
    TextureHandle handle;
    ResourceUsage usage = ResourceUsage::None;
};

// Cached attachment data captured during setup. Stored on the Pass so the
// graph can build a RenderPassDesc at execute() time without re-running the
// user's setup callback.
struct ColorAttachmentSpec {
    TextureHandle handle;
    rhi::LoadAction  load_action  = rhi::LoadAction::Clear;
    rhi::StoreAction store_action = rhi::StoreAction::Store;
    float            clear_color[4] = {0, 0, 0, 1};
};

struct DepthAttachmentSpec {
    TextureHandle handle;
    rhi::LoadAction  load_action  = rhi::LoadAction::Clear;
    rhi::StoreAction store_action = rhi::StoreAction::DontCare;
    float            clear_depth  = 1.0f;
};

// Runtime context passed to the pass's execute lambda. The graph wires this
// up just before invoking the user callback. Owns no resources.
class RenderContext {
public:
    RenderContext(rhi::CommandBuffer& cmd, FrameGraph& graph) noexcept
        : cmd_(cmd), graph_(graph) {}

    [[nodiscard]] rhi::CommandBuffer& cmd() noexcept { return cmd_; }
    [[nodiscard]] FrameGraph&         graph() noexcept { return graph_; }

    // Realized RHI texture for a virtual handle. Throws via assert if the
    // handle has not been allocated by compile().
    [[nodiscard]] rhi::Texture& texture(TextureHandle h);

    // Pre-built RenderPassDesc for this pass (color + depth attachments per
    // the pass's setup declarations). Convenience for the common case.
    [[nodiscard]] rhi::RenderPassDesc make_render_pass_desc();

    // Internal - set by FrameGraph::execute() before invoking the lambda.
    void _set_current_pass(std::uint32_t pass_index) noexcept { pass_index_ = pass_index; }

private:
    rhi::CommandBuffer& cmd_;
    FrameGraph&         graph_;
    std::uint32_t       pass_index_ = 0;
};

using PassExecuteFn = std::function<void(RenderContext&)>;

// Builder passed into the user's setup callback. Records what the pass
// reads/writes, plus convenience helpers for attachments.
class PassBuilder {
public:
    explicit PassBuilder(FrameGraph& graph, std::uint32_t pass_index) noexcept
        : graph_(graph), pass_index_(pass_index) {}

    void read(TextureHandle h, ResourceUsage usage = ResourceUsage::ShaderRead);
    void write(TextureHandle h, ResourceUsage usage = ResourceUsage::ColorAttachment);

    void write_color(TextureHandle h, rhi::LoadAction load = rhi::LoadAction::Clear,
                      float r = 0, float g = 0, float b = 0, float a = 1);
    void write_depth(TextureHandle h, rhi::LoadAction load = rhi::LoadAction::Clear,
                      float clear_depth = 1.0f);

private:
    FrameGraph&   graph_;
    std::uint32_t pass_index_;
};

using PassSetupFn = std::function<void(PassBuilder&)>;

struct Pass {
    std::string                       name;
    PassExecuteFn                     execute;
    std::vector<ResourceBinding>      reads;
    std::vector<ResourceBinding>      writes;
    std::vector<ColorAttachmentSpec>  colors;
    DepthAttachmentSpec               depth;
    bool                              has_depth = false;
};

}  // namespace mge::frame_graph
