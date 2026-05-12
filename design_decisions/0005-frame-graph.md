# ADR-0005 — Frame Graph as the renderer's central abstraction

- **Status**: accepted
- **Date**: 2026-05-12
- **Tags**: renderer, architecture

## Context

Modern engines centralize render-pass scheduling, resource lifetimes, and barriers in a **frame graph** (also called render graph). Examples: EA Frostbite, id Tech 7, O3DE, Bevy, Filament. Without it, transient resources tend to leak across subsystems, barriers become tribal knowledge, and adding new passes is a refactoring exercise.

## Decision

Implement a **frame graph** as the single source of truth for:

- Pass declaration (reads / writes / side effects).
- Transient resource allocation with **aliasing** when virtual lifetimes don't overlap.
- Automatic insertion of resource transitions / barriers from declared usage.
- Topological sort and (future) parallel-schedule annotation.
- Debug visualization (Graphviz dump + per-pass GPU timestamps).

Phase 1 ships **v1**: single graphics queue, no async compute, no split barriers, no subpasses. v2 (post-M9 / Phase 1.5) adds compute queue, async-eligible pass scheduling, and (optionally) tile shading / sub-passes.

## Alternatives considered

### Hand-coded passes with manual barriers

- **Pros**: minimal upfront effort.
- **Cons**: barriers are correct-by-prayer, transient allocation isn't pooled, can't profile cleanly.
- **Why it lost**: doesn't scale past 3–4 passes.

### Off-the-shelf frame graph (Filament's, RPS)

- **Pros**: existing, battle-tested.
- **Cons**: too coupled to other engines' types; integrating costs as much as writing v1 for our RHI.
- **Why it lost**: doesn't fit our RHI cleanly. Keep as reference reading.

## Consequences

- Passes are pure functions of declared inputs.
- Transient VRAM is shared automatically.
- Frame graph dump becomes a primary debugging tool.
- A wrong pass declaration becomes a barrier bug — caught by integration tests + Metal validation layer.

## Tradeoffs

- **Cost**: ~1 week to ship v1 (M5).
- **Performance**: positive on VRAM, neutral on CPU if implementation is allocation-aware.
- **Debuggability**: large positive.

## Implementation notes

- `engine/renderer/frame_graph/`:
  - `frame_graph.h` — Graph type, build & compile entry points.
  - `pass.h`, `pass_builder.h` — pass declaration API.
  - `resource.h` — virtual resource handles.
  - `transient_pool.h` — physical allocator for aliased transients.
  - `dot_dump.cpp` — debug graphviz export.
- Construction phase: user code submits pass declarations.
- Compile phase: topological sort, lifetime analysis, alias map.
- Execute phase: walk passes, transition resources, call recorded lambdas.

## Open questions

- Resource versioning (RaTS / Yuriy's approach) vs. usage-bitmask. Decide at M5.
- Cross-frame history textures (TAA history): expose as graph "imported" resources with explicit lifetime hint.

## References

- Yuriy O'Donnell, "FrameGraph: Extensible Rendering Architecture in Frostbite" (GDC 2017).
- Riccardo Loggini, "Render Graphs" series.
- O3DE Atom render graph documentation.
