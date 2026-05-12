# ADR-0013 — Compute-driven skinning (replace vertex-shader skinning)

- **Status**: accepted
- **Date**: 2026-05-12
- **Deciders**: renderer
- **Tags**: compute, skinning, phase-1.5

## Context

M16 shipped vertex-shader skinning. That works for one pass but rebuilds the same deformed mesh in every draw — shadow map + G-Buffer + reflection (when dynamic TLAS lands) would each re-run the joint blends. Modern engines pre-skin once into a deformed vertex buffer; every pass that consumes the skinned mesh reads that buffer like any other rigid mesh.

M17 is the milestone for that move. We already have the compute foundations from M12 (RHI ComputePipeline / ComputeEncoder) and the bone/animation infrastructure from M16. The piece left is the kernel + plumbing.

## Decision

- **Pre-skin in a compute pass** that runs once per frame before the gbuffer pass. Inputs: `SkinnedVertex` source buffer + `JointBuffer` for the current pose. Output: `PbrVertex` (position + normal) into a Private GPU buffer.
- **Downstream consumers see a rigid mesh.** The G-Buffer pass binds the compute output as a normal vertex buffer and uses the standard `gbuffer_pso`. The skinning shader (`k_skinned_gbuffer_msl`) is removed entirely.
- **Output buffer is Private storage** — written by compute, read by the vertex stage. No CPU readback. No copies.
- **Ordering established by declaration order** in the FrameGraph (FG doesn't model buffer-resource dependencies yet — P1-FG-BUFFER-001 still open). The `skin` pass is declared before `gbuffer` so it's scheduled first.

## Alternatives considered

1. **Keep vertex-shader skinning.** Already shipped in M16. Pros: simpler, no intermediate buffer. Cons: redundant when multiple passes use the same skinned mesh; doesn't support shadow / RT consumption of skinned geometry. **Replaced.**

2. **Skin into a tile-memory buffer.** Apple's TBDR has persistent tile memory; skinning could in theory write to it for the same render pass to consume. Cons: scope mismatch — tile memory is per-tile per render pass, not per-frame across passes. **Rejected.**

3. **Dual-source skinning (compute for shadow / RT, vertex for gbuffer).** Pros: no `skinned_vbuf` allocation when nothing else needs it. Cons: two code paths, two bug surfaces. **Rejected.**

4. **CPU pre-skin + upload.** Pros: zero new GPU work. Cons: bus traffic, single-thread. **Rejected** for any non-trivial mesh.

## Consequences

**Easy now:**
- The skinned tube is a "rigid mesh whose verts happen to change every frame" from the perspective of every consumer. Adding shadow casting or RT visibility for it requires zero new skinning code — just plumb the buffer through.
- The compute output is the natural source for a future per-frame BLAS rebuild (dynamic TLAS, P1-RT-STATIC-TLAS-001 fix).

**Committed:**
- One `PbrVertex` output buffer per skinned mesh. Modest memory (~13 KB for the tube).
- The compute pass is sequenced via declaration order; the FG's lack of buffer handles means a buggy reorder of passes could break it silently. Tracked in P1-FG-BUFFER-001.

**Explicitly not committing to:**
- Multi-instanced skinned meshes (one buffer per instance). Today the tube is one instance.
- Dynamic TLAS — still tracked in P1-RT-STATIC-TLAS-001.
- Tile-memory skinning, mesh shaders, etc.

## Tradeoffs

- **GPU**: compute pass is ~400 threads × (one 4-bone blend each) — well under 0.05 ms on M1 Pro. Vertex stage drop is comparable. Net: a wash for one consumer (gbuffer), a win for two+ (gbuffer + shadow + RT).
- **Memory**: + ~13 KB Private buffer for the tube.
- **Code**: removed `k_skinned_gbuffer_msl` + `skinned_gbuffer_pso`; added `k_skin_compute_msl` + `skin_compute_pso` + `tube_skinned_vbuf` + `tube_skin_count_buf`.
- **Future work it unlocks**: shadow casting for skinned mesh, dynamic-TLAS BLAS rebuild from the same buffer, per-frame mesh-LOD-aware skinning.

## Open questions

- When to add `BufferHandle` to the FrameGraph? Probably with the next compute-producer/render-consumer pair that isn't trivially co-located.
- Multi-instance skinning shape: structured buffer keyed by instance, or argument buffer? Lift when needed.
- Dual-quaternion vs linear-blend skinning: deferred from M16, still deferred here.

## Implementation notes

- `examples/hello_metal/main.cpp` —
  - `k_skin_compute_msl` — kernel reads `SkinnedVertex` + `JointBuffer`, writes `PbrVertex` (`packed_float3 + pad`). One thread per vertex.
  - `DeferredRenderer::skin_compute_shader` / `skin_compute_pso`.
  - `tube_skinned_vbuf` (Private), `tube_skin_count_buf` (Shared uniform with `uint count`).
  - `fg.add_pass("skin", …)` runs the compute before the gbuffer pass.
  - Gbuffer pass tube draw uses `gbuffer_pso` + `tube_skinned_vbuf`. Skinned-PSO + skinning shader deleted.

## Phase 1.5 closeout

Phase 1.5 wraps with M17 in. The Phase 1.5 vertical slice now includes:

- M12 — Compute pipelines + GPU-driven particles
- M13 — Inline ray-traced shadows + metallic reflections
- M14 — HZB occlusion culling (build + stats, M14.b owns actual cull)
- M15 — Discrete LOD chain for the sphere mesh
- M16 — Skeletal animation with vertex-shader skinning
- M17 — Compute-driven skinning (this commit)

Phase 2 picks up the asset pipeline (glTF, textures, animation files), dynamic TLAS, GPU-driven draw via indirect, and the ECS proper.

## References

- ADR-0008 — Compute pipelines.
- ADR-0012 — Vertex-shader skinning (superseded by this ADR for the demo path; the math/conventions still apply).
- glTF 2.0 skinning — same linear-blend math; compute kernel is the natural place to run it.
