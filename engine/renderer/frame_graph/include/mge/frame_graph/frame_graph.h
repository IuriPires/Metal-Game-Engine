#pragma once

#include "mge/frame_graph/handles.h"
#include "mge/frame_graph/pass.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mge::rhi {
class Device;
class Queue;
class Texture;
class SwapchainFrame;
}  // namespace mge::rhi

namespace mge::frame_graph {

// Phase 1 v1: single graphics queue, no async compute, no split barriers, no
// subpasses. Compiles a topological schedule with simple lifetime-based
// aliasing for transient textures. See ADR-0005.
class FrameGraph {
public:
    explicit FrameGraph(rhi::Device& device) noexcept;
    ~FrameGraph();

    FrameGraph(const FrameGraph&)            = delete;
    FrameGraph& operator=(const FrameGraph&) = delete;

    // Resource declarations
    [[nodiscard]] TextureHandle import_texture(rhi::Texture& tex, std::string name = "imported");
    [[nodiscard]] TextureHandle create_texture(TransientTextureDesc desc,
                                                std::string name = "transient");

    // Pass declarations
    void add_pass(std::string name, PassSetupFn setup, PassExecuteFn execute);

    // Compile: topo sort + lifetime + alias plan. Returns false if there is a
    // cycle in the read/write dependency graph.
    [[nodiscard]] bool compile();

    // Execute: builds a command buffer from the supplied queue and runs the
    // scheduled passes. compile() must have succeeded since the last reset().
    // If `present` is non-null, the command buffer presents the drawable
    // before commit, so the same buffer can render and present in one go.
    void execute(rhi::Queue& queue, rhi::SwapchainFrame* present = nullptr);

    // Empty all passes and clear allocated transients. Call between frames if
    // re-declaring the graph from scratch each frame.
    void reset() noexcept;

    // Graphviz dump of the compiled graph (passes as boxes, resources as
    // ellipses, edges colored by usage). Useful for `dot` rendering and CI
    // attachment.
    [[nodiscard]] std::string to_dot() const;

    // --- access used by Pass + RenderContext + PassBuilder ---
    [[nodiscard]] rhi::Texture& realize(TextureHandle h);
    [[nodiscard]] const TransientTextureDesc& transient_desc(TextureHandle h) const;
    [[nodiscard]] const std::string&          resource_name(TextureHandle h) const;
    [[nodiscard]] const Pass&                 pass(std::uint32_t index) const;
    Pass&                                     pass(std::uint32_t index);

    // Stats for tests / overlay
    [[nodiscard]] std::size_t num_passes() const noexcept { return passes_.size(); }
    [[nodiscard]] std::size_t num_resources() const noexcept { return resources_.size(); }
    [[nodiscard]] std::size_t num_physical_textures() const noexcept { return physicals_.size(); }
    [[nodiscard]] const std::vector<std::uint32_t>& schedule() const noexcept { return schedule_; }
    // For aliasing tests: returns the physical texture slot assigned to a
    // virtual handle. Two virtual handles with the same physical_slot share
    // memory.
    [[nodiscard]] std::uint32_t physical_slot(TextureHandle h) const;

private:
    struct Resource {
        std::string name;
        bool        is_imported    = false;
        // Imported:
        rhi::Texture* imported = nullptr;
        // Transient:
        TransientTextureDesc desc{};
        // Alias plan: which physical texture this virtual resource ends up on.
        std::uint32_t physical_slot = std::numeric_limits<std::uint32_t>::max();
        // Lifetime (phase indexes into `schedule_`)
        std::uint32_t first_use = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t last_use  = 0;
    };

    [[nodiscard]] bool topological_sort();
    void               compute_lifetimes();
    void               assign_aliases();
    void               allocate_physicals();
    void               release_physicals() noexcept;

    rhi::Device&                              device_;
    std::vector<Resource>                     resources_;
    std::vector<Pass>                         passes_;
    std::vector<std::uint32_t>                schedule_;             // pass indices in execute order
    // Physical textures (owned). Indexed by Resource::physical_slot.
    std::vector<std::unique_ptr<rhi::Texture>> physicals_;
    bool                                       compiled_ = false;

    friend class PassBuilder;
};

}  // namespace mge::frame_graph
