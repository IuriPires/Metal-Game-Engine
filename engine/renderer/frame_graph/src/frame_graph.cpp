#include "mge/frame_graph/frame_graph.h"

#include "mge/core/assert.h"
#include "mge/rhi/command_buffer.h"
#include "mge/rhi/device.h"
#include "mge/rhi/queue.h"
#include "mge/rhi/swapchain.h"
#include "mge/rhi/texture.h"

#include <algorithm>
#include <cstring>
#include <fmt/format.h>
#include <unordered_map>

namespace mge::frame_graph {

namespace {

constexpr std::uint32_t kInvalid = std::numeric_limits<std::uint32_t>::max();

bool desc_compatible(const TransientTextureDesc& a, const TransientTextureDesc& b) noexcept {
    return a.width   == b.width   &&
           a.height  == b.height  &&
           a.mip_levels == b.mip_levels &&
           a.format  == b.format  &&
           static_cast<std::uint32_t>(a.usage)   == static_cast<std::uint32_t>(b.usage) &&
           static_cast<std::uint32_t>(a.storage) == static_cast<std::uint32_t>(b.storage);
}

}  // namespace

// =====================================================================
//   PassBuilder
// =====================================================================

void PassBuilder::read(TextureHandle h, ResourceUsage usage) {
    MGE_ASSERT(h.valid(), "PassBuilder::read with invalid handle");
    graph_.pass(pass_index_).reads.push_back({h, usage});
}

void PassBuilder::write(TextureHandle h, ResourceUsage usage) {
    MGE_ASSERT(h.valid(), "PassBuilder::write with invalid handle");
    graph_.pass(pass_index_).writes.push_back({h, usage});
}

void PassBuilder::write_color(TextureHandle h, rhi::LoadAction load,
                                float r, float g, float b, float a) {
    auto& p = graph_.pass(pass_index_);
    p.writes.push_back({h, ResourceUsage::ColorAttachment});
    ColorAttachmentSpec spec;
    spec.handle         = h;
    spec.load_action    = load;
    spec.store_action   = rhi::StoreAction::Store;
    spec.clear_color[0] = r;
    spec.clear_color[1] = g;
    spec.clear_color[2] = b;
    spec.clear_color[3] = a;
    p.colors.push_back(spec);
}

void PassBuilder::write_depth(TextureHandle h, rhi::LoadAction load, float clear_depth) {
    auto& p = graph_.pass(pass_index_);
    p.writes.push_back({h, ResourceUsage::DepthAttachment});
    p.depth.handle       = h;
    p.depth.load_action  = load;
    // Store by default - safer to keep the depth contents than to assume the
    // user won't sample them later. M6's deferred lighting pass reads depth
    // as a shader resource; with DontCare the values were garbage.
    // Phase 1.5 can fold this into the FG's lifetime analysis (DontCare when
    // no later pass reads the depth).
    p.depth.store_action = rhi::StoreAction::Store;
    p.depth.clear_depth  = clear_depth;
    p.has_depth          = true;
}

// =====================================================================
//   RenderContext
// =====================================================================

rhi::Texture& RenderContext::texture(TextureHandle h) {
    return graph_.realize(h);
}

rhi::RenderPassDesc RenderContext::make_render_pass_desc() {
    const auto& p = graph_.pass(pass_index_);
    rhi::RenderPassDesc rp;
    rp.label = p.name;

    rp.num_color_attachments =
        static_cast<std::uint32_t>(p.colors.size() < 8 ? p.colors.size() : 8);
    for (std::uint32_t i = 0; i < rp.num_color_attachments; ++i) {
        const auto& c = p.colors[i];
        rp.color_attachments[i].texture        = &graph_.realize(c.handle);
        rp.color_attachments[i].load_action    = c.load_action;
        rp.color_attachments[i].store_action   = c.store_action;
        rp.color_attachments[i].clear_color[0] = c.clear_color[0];
        rp.color_attachments[i].clear_color[1] = c.clear_color[1];
        rp.color_attachments[i].clear_color[2] = c.clear_color[2];
        rp.color_attachments[i].clear_color[3] = c.clear_color[3];
    }

    if (p.has_depth) {
        rp.has_depth                      = true;
        rp.depth_attachment.texture       = &graph_.realize(p.depth.handle);
        rp.depth_attachment.load_action   = p.depth.load_action;
        rp.depth_attachment.store_action  = p.depth.store_action;
        rp.depth_attachment.clear_depth   = p.depth.clear_depth;
    }
    return rp;
}

// =====================================================================
//   FrameGraph
// =====================================================================

FrameGraph::FrameGraph(rhi::Device& device) noexcept : device_(device) {}

FrameGraph::~FrameGraph() { release_physicals(); }

TextureHandle FrameGraph::import_texture(rhi::Texture& tex, std::string name) {
    Resource r;
    r.name        = std::move(name);
    r.is_imported = true;
    r.imported    = &tex;
    resources_.push_back(std::move(r));
    return TextureHandle{static_cast<std::uint32_t>(resources_.size() - 1)};
}

TextureHandle FrameGraph::create_texture(TransientTextureDesc desc, std::string name) {
    Resource r;
    r.name        = std::move(name);
    r.is_imported = false;
    r.desc        = desc;
    resources_.push_back(std::move(r));
    return TextureHandle{static_cast<std::uint32_t>(resources_.size() - 1)};
}

void FrameGraph::add_pass(std::string name, PassSetupFn setup, PassExecuteFn execute) {
    Pass p;
    p.name    = std::move(name);
    p.execute = std::move(execute);
    passes_.push_back(std::move(p));

    PassBuilder b(*this, static_cast<std::uint32_t>(passes_.size() - 1));
    if (setup) setup(b);
}

bool FrameGraph::compile() {
    compiled_ = false;
    if (!topological_sort()) {
        return false;
    }
    compute_lifetimes();
    assign_aliases();
    allocate_physicals();
    compiled_ = true;
    return true;
}

bool FrameGraph::topological_sort() {
    schedule_.clear();
    if (passes_.empty()) {
        return true;
    }

    // Walk passes in declaration order. For each read R, the producer is the
    // MOST RECENT writer up to (but not including) this pass — captures the
    // "use what was written just before me" intent and avoids cycles when the
    // same resource is rewritten later (e.g. bloom mips read by downsample
    // then re-written by upsample).
    //
    // For writes, we also chain write-after-write into an ordering edge so
    // overlapping writers stay in declaration order.
    const std::size_t              n = passes_.size();
    std::vector<std::vector<std::uint32_t>> adj(n);
    std::vector<std::uint32_t>     indeg(n, 0);

    auto add_edge = [&](std::uint32_t from, std::uint32_t to) {
        adj[from].push_back(to);
        ++indeg[to];
    };

    std::unordered_map<std::uint32_t, std::uint32_t> current_writer;
    for (std::uint32_t i = 0; i < n; ++i) {
        for (const auto& r : passes_[i].reads) {
            auto it = current_writer.find(r.handle.id);
            if (it != current_writer.end() && it->second != i) {
                add_edge(it->second, i);
            }
        }
        for (const auto& w : passes_[i].writes) {
            auto it = current_writer.find(w.handle.id);
            if (it != current_writer.end() && it->second != i) {
                add_edge(it->second, i);  // WAW: serialize writers
            }
            current_writer[w.handle.id] = i;
        }
    }

    // Kahn's algorithm, stable on insertion order.
    std::vector<std::uint32_t> ready;
    for (std::uint32_t i = 0; i < n; ++i) {
        if (indeg[i] == 0) {
            ready.push_back(i);
        }
    }
    std::sort(ready.begin(), ready.end());

    schedule_.reserve(n);
    while (!ready.empty()) {
        const std::uint32_t v = ready.front();
        ready.erase(ready.begin());
        schedule_.push_back(v);
        for (std::uint32_t u : adj[v]) {
            if (--indeg[u] == 0) {
                ready.push_back(u);
            }
        }
        std::sort(ready.begin(), ready.end());
    }
    return schedule_.size() == n;
}

void FrameGraph::compute_lifetimes() {
    for (auto& r : resources_) {
        r.first_use = kInvalid;
        r.last_use  = 0;
    }
    for (std::uint32_t phase = 0; phase < schedule_.size(); ++phase) {
        const std::uint32_t pass_idx = schedule_[phase];
        const auto&         p        = passes_[pass_idx];
        auto                touch    = [&](TextureHandle h) {
            auto& r = resources_[h.id];
            if (phase < r.first_use) r.first_use = phase;
            if (phase > r.last_use)  r.last_use  = phase;
        };
        for (const auto& w : p.writes) touch(w.handle);
        for (const auto& r : p.reads)  touch(r.handle);
    }
}

void FrameGraph::assign_aliases() {
    // Imported resources keep an external pointer; physical_slot is set to a
    // sentinel for them so that allocate_physicals() can skip them.
    for (auto& r : resources_) {
        r.physical_slot = kInvalid;
    }

    struct Slot {
        TransientTextureDesc desc;
        std::uint32_t        last_use = 0;  // phase index of the most recent user
        bool                 used     = false;
    };
    std::vector<Slot> slots;

    // Pack in order of first_use to maximize reuse.
    std::vector<std::uint32_t> order;
    order.reserve(resources_.size());
    for (std::uint32_t i = 0; i < resources_.size(); ++i) {
        if (!resources_[i].is_imported && resources_[i].first_use != kInvalid) {
            order.push_back(i);
        }
    }
    std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
        return resources_[a].first_use < resources_[b].first_use;
    });

    for (std::uint32_t i : order) {
        auto& r = resources_[i];
        std::uint32_t best_slot = kInvalid;
        for (std::uint32_t s = 0; s < slots.size(); ++s) {
            if (slots[s].used && slots[s].last_use < r.first_use &&
                desc_compatible(slots[s].desc, r.desc)) {
                best_slot = s;
                break;
            }
        }
        if (best_slot == kInvalid) {
            slots.push_back(Slot{r.desc, r.last_use, true});
            r.physical_slot = static_cast<std::uint32_t>(slots.size() - 1);
        } else {
            slots[best_slot].last_use = std::max(slots[best_slot].last_use, r.last_use);
            r.physical_slot           = best_slot;
        }
    }
    // Save count of physicals via slot count.
    physicals_.clear();
    physicals_.resize(slots.size());
    // Stash desc-of-each-slot indirectly by remembering an example resource per
    // slot in allocate_physicals().
}

void FrameGraph::allocate_physicals() {
    // First non-imported resource per physical slot dictates the desc.
    std::vector<const TransientTextureDesc*> slot_desc(physicals_.size(), nullptr);
    for (auto& r : resources_) {
        if (!r.is_imported && r.physical_slot != kInvalid && slot_desc[r.physical_slot] == nullptr) {
            slot_desc[r.physical_slot] = &r.desc;
        }
    }

    for (std::uint32_t s = 0; s < physicals_.size(); ++s) {
        if (slot_desc[s] == nullptr) continue;
        const auto& d = *slot_desc[s];
        rhi::TextureDesc td;
        td.width      = d.width;
        td.height     = d.height;
        td.mip_levels = d.mip_levels;
        td.format     = d.format;
        td.usage      = d.usage;
        td.storage    = d.storage;
        td.label      = fmt::format("fg.transient[{}]", s);
        physicals_[s] = device_.create_texture(td);
    }
}

void FrameGraph::release_physicals() noexcept {
    physicals_.clear();
}

void FrameGraph::execute(rhi::Queue& queue, rhi::SwapchainFrame* present) {
    MGE_ASSERT(compiled_, "FrameGraph::execute called before compile()");

    rhi::CommandBuffer cmd = queue.create_command_buffer();
    RenderContext      ctx(cmd, *this);

    for (std::uint32_t pass_idx : schedule_) {
        ctx._set_current_pass(pass_idx);
        const auto& p = passes_[pass_idx];
        if (p.execute) {
            p.execute(ctx);
        }
    }
    if (present != nullptr) {
        cmd.present(*present);
    }
    cmd.commit();
}

void FrameGraph::reset() noexcept {
    passes_.clear();
    resources_.clear();
    schedule_.clear();
    release_physicals();
    compiled_ = false;
}

rhi::Texture& FrameGraph::realize(TextureHandle h) {
    MGE_ASSERT(h.valid() && h.id < resources_.size(), "realize: invalid handle");
    auto& r = resources_[h.id];
    if (r.is_imported) {
        MGE_ASSERT(r.imported != nullptr, "imported resource is null");
        return *r.imported;
    }
    MGE_ASSERT(r.physical_slot != kInvalid && r.physical_slot < physicals_.size(),
                "transient resource not allocated - compile() must have failed");
    MGE_ASSERT(physicals_[r.physical_slot] != nullptr, "physical slot empty");
    return *physicals_[r.physical_slot];
}

const TransientTextureDesc& FrameGraph::transient_desc(TextureHandle h) const {
    return resources_[h.id].desc;
}

const std::string& FrameGraph::resource_name(TextureHandle h) const {
    return resources_[h.id].name;
}

const Pass& FrameGraph::pass(std::uint32_t index) const { return passes_[index]; }
Pass&       FrameGraph::pass(std::uint32_t index)       { return passes_[index]; }

std::uint32_t FrameGraph::physical_slot(TextureHandle h) const {
    return resources_[h.id].physical_slot;
}

std::string FrameGraph::to_dot() const {
    fmt::memory_buffer out;
    fmt::format_to(std::back_inserter(out), "digraph FrameGraph {{\n");
    fmt::format_to(std::back_inserter(out), "  rankdir=LR;\n");
    fmt::format_to(std::back_inserter(out), "  node [fontname=\"Helvetica\"];\n\n");

    // Passes - rectangles, in schedule order.
    fmt::format_to(std::back_inserter(out), "  // passes\n");
    for (std::uint32_t i = 0; i < passes_.size(); ++i) {
        fmt::format_to(std::back_inserter(out),
                       "  pass_{} [shape=box style=filled fillcolor=lightblue label=\"{}\"];\n",
                       i, passes_[i].name);
    }

    // Resources - ellipses, imported in green, transients colored by physical slot.
    fmt::format_to(std::back_inserter(out), "\n  // resources\n");
    for (std::uint32_t i = 0; i < resources_.size(); ++i) {
        const auto& r = resources_[i];
        if (r.is_imported) {
            fmt::format_to(std::back_inserter(out),
                           "  res_{} [shape=ellipse style=filled fillcolor=palegreen "
                           "label=\"{} (imported)\"];\n",
                           i, r.name);
        } else {
            fmt::format_to(std::back_inserter(out),
                           "  res_{} [shape=ellipse style=filled fillcolor=lightyellow "
                           "label=\"{}\\n(slot {})\"];\n",
                           i, r.name, r.physical_slot);
        }
    }

    // Edges - writes (pass -> resource, solid), reads (resource -> pass, dashed).
    fmt::format_to(std::back_inserter(out), "\n  // edges\n");
    for (std::uint32_t i = 0; i < passes_.size(); ++i) {
        for (const auto& w : passes_[i].writes) {
            fmt::format_to(std::back_inserter(out),
                           "  pass_{} -> res_{} [color=blue];\n", i, w.handle.id);
        }
        for (const auto& r : passes_[i].reads) {
            fmt::format_to(std::back_inserter(out),
                           "  res_{} -> pass_{} [style=dashed color=darkgreen];\n",
                           r.handle.id, i);
        }
    }

    fmt::format_to(std::back_inserter(out), "}}\n");
    return fmt::to_string(out);
}

}  // namespace mge::frame_graph
