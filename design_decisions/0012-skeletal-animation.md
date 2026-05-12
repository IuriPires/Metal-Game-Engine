# ADR-0012 — Vertex-shader skinning with a procedural bone chain

- **Status**: accepted
- **Date**: 2026-05-12
- **Deciders**: renderer
- **Tags**: animation, skinning, phase-1.5

## Context

M16 owns skeletal animation. A real engine eventually wants:

- Animation files (glTF channels, FBX, custom)
- Skeleton hierarchies with arbitrary bone counts
- Compute skinning so the deformed mesh can be shared by multiple passes (shadow + gbuffer + depth pre-pass) without re-skinning
- IK, retargeting, blending, layered animations

For Phase 1.5 we need to land the foundation — vertex format, joint matrices, skinning math, animation update loop — without committing to an asset pipeline that doesn't exist yet. The next milestone, M17 (skinning compute), is in the ROADMAP specifically to move skinning off the vertex stage when we want to share results across passes.

## Decision

- **Vertex-shader skinning, 4 influences per vertex.** Each `SkinnedVertex` packs position, normal, 4 weights, and 4 joint indices (64 bytes total, 16-byte aligned). The skinned gbuffer vertex shader reads the joint matrices from a uniform-style buffer at slot 3, blends 4 weighted joint transforms per vertex, then composes with the per-instance model matrix.
- **Procedural mesh + procedural skeleton in v1.** `make_skinned_tube(radial, length, height, radius)` generates a cylinder along +Y with per-vertex weights blending the two nearest bones along the spine. `kSkinnedTubeBones = 6`. No glTF, no animation files.
- **Procedural animation.** `solve_bone_chain(time, span, out_joints)` walks the chain root → tip, applying a per-bone `rotation_x(sin(t * 2.5 + bone_idx * 0.55) * 0.32)`. Each joint's output matrix is `world * inverse_bind`, which is the right thing to pre-multiply with the bind-pose vertex position.
- **Reuses the rigid gbuffer fragment shader** — only the vertex stage differs.

The skinned tube is **not** in the RT TLAS. Reflections + ray-shadows ignore it. This is tracked alongside the static-TLAS work — adding dynamic geometry to the TLAS is the same project (P1-RT-STATIC-TLAS-001).

M17 (compute skinning) will:

- Move skinning into a compute pass that writes a deformed `PbrVertex` buffer.
- Share that buffer across shadow + gbuffer + RT (once dynamic TLAS lands).
- Decouple skinning cost from draw-call count (vertex stage runs once per pass; compute runs once per frame).

## Alternatives considered

1. **CPU skinning, upload deformed verts each frame.** Pros: zero new GPU work, easiest to debug. Cons: bus traffic scales with vertex count, can't share with shadow / RT without the same upload. **Rejected** for v1.

2. **Compute skinning from the start (collapse M16 + M17).** Pros: right architecture for AAA workloads. Cons: bigger one-commit scope; needs an output vertex buffer per skinned instance; coordination with FrameGraph. **Deferred to M17.**

3. **Dual quaternion skinning.** Avoids the well-known volume-loss artifacts of linear blend skinning at large joint angles. Pros: better visual quality. Cons: more math, more uniform memory, more debugging. **Out of scope.**

4. **glTF loader to source the skeleton + animation.** Pros: real data. Cons: cgltf integration is its own milestone (Phase 2 asset pipeline). **Deferred.**

## Consequences

**Easy now:**
- The vertex format + shader pattern is correct for any future skinned mesh; only the geometry source changes.
- `solve_bone_chain` is a placeholder, but the joint-matrix contract (`world_pose × inverse_bind`) is the standard one — animation files will drop in cleanly later.
- Linear-blend skinning math is exactly what glTF expects, so future imports won't change the runtime.

**Committed:**
- 64-byte vertex stride for skinned meshes (vs 32 for rigid). 2× memory on skinned geometry only.
- Joint buffer is uniform-style, single instance — multi-instanced skinned meshes will want either a structured buffer keyed by `instance_id` or an argument buffer.

**Explicitly not committing to:**
- Compute skinning (M17 owns it).
- Dynamic TLAS — skinned tube is invisible to RT shadows + reflections (P1-SKIN-RT-001).
- Skinned shadow casting — tube doesn't cast a shadow into the CSM shadow map. With RT shadows on (the default), it would cast if the BLAS existed; same fix path as RT visibility.
- glTF animation import.

## Tradeoffs

- **CPU**: 6 matrix multiplies per frame for the demo. <1 µs.
- **GPU**: 4 mat4 × vec4 mads per vertex. Trivial.
- **Memory**: skinned tube is ~400 verts × 64 bytes = 25 KB. Joint buffer = 384 B. Tiny.
- **Visual deficit acknowledged**: tube has no RT shadow + no RT reflection. Looks "floaty" against the cubes that do cast shadows.

## Open questions

- When to switch to compute skinning? Whenever we want shadow + gbuffer + RT to all consume the same deformed verts.
- Joint indexing strategy at scale — uniform array, structured buffer, argument buffer? Tied to the shading-asset story.
- Animation system shape — channels, tracks, blending. Probably a Phase 2 topic after glTF lands.

## Implementation notes

- `examples/hello_metal/main.cpp` —
  - `SkinnedVertex` (64 B, aligned 16) at file scope.
  - `kSkinnedTubeBones = 6`, `make_skinned_tube`, `JointBuffer`, `solve_bone_chain` helpers.
  - `k_skinned_gbuffer_msl` — skinning vertex shader; reuses rigid `gbuffer_fs`.
  - `DeferredRenderer::tube_vbuf` / `tube_ibuf` / `tube_joint_buf` / `skinned_gbuffer_pso`.
  - Tube instance lives at `tube_slot = k_spheres.size() + 1`; cubes start at `cube_base = tube_slot + 1`.
  - G-Buffer pass draws the tube last with `skinned_gbuffer_pso` and the joint buffer bound at vertex slot 3.
  - HUD line `SKIN BONES N  VERTS X`.

## References

- ADR-0008 — Compute pipelines (M17 will live here).
- ADR-0009 — Ray tracing (skinned mesh visibility deferred).
- glTF 2.0 spec — Skin / Animation chapters; describes the matrix conventions we're already matching.
- "Skeletal Animation" chapter in *Real-Time Rendering 4e* (refs/).
