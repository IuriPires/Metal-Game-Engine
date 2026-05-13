#pragma once

// M27 — Viewport object picking. Closest-hit ray-vs-AABB selection in CPU
// land. The demo builds a `Pickable[]` each frame (one per selectable scene
// entity), then calls `closest_hit()` from the click handler to find which
// entity the user pointed at. Selection metadata (`tag` + `index`) is
// opaque to the picker — the demo decides what it means.

#include "mge/math/aabb.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace mge::scene {

// One scene entity that can be picked. The picker only cares about the
// AABB; the `tag` + `index` ride along untouched so the caller can map a
// hit back to its own data structures (e.g. tag = SelectionKind enum,
// index = "which sphere" or "which cube").
struct Pickable {
    math::Aabb    aabb;
    std::uint32_t tag   = 0;
    std::uint32_t index = 0;
};

struct PickHit {
    std::size_t pickable_index = 0;   // index into the input span
    float       t              = 0.0f; // ray param at the nearest face hit
};

// Closest-hit ray test against a list of pickables. Returns `nullopt` when
// nothing was hit. Iterating is O(n) — fine for the demo's ~1k entities;
// any future BVH lives at a higher layer.
[[nodiscard]] inline std::optional<PickHit>
closest_hit(const math::Ray& r, std::span<const Pickable> pickables) noexcept {
    std::optional<PickHit> best;
    for (std::size_t i = 0; i < pickables.size(); ++i) {
        const auto h = math::ray_aabb_intersect(r, pickables[i].aabb);
        if (!h.hit) continue;
        if (!best || h.t_near < best->t) {
            best = PickHit{i, h.t_near};
        }
    }
    return best;
}

}  // namespace mge::scene
