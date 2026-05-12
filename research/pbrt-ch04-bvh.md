# PBRT — Chapter 4: Primitives and Intersection Acceleration

> Notes for Phase 1.5 ray tracing. Reading PBRT in advance to avoid surprises when we wire Metal acceleration structures.

## Why BVH

Ray-scene intersection naive cost is O(N) per ray, untenable at scene scale. Spatial acceleration structures reduce expected intersection cost to ~O(log N). PBRT covers grids, BVHs, and kd-trees; consensus today (game engines, NVIDIA RTX, Metal MPS) is **BVH** because it deals well with overlapping primitives and animates cheaply.

## BVH essentials

- Tree of axis-aligned bounding boxes; leaves contain primitives.
- Construction: top-down (SAH), bottom-up, or incremental.
- Quality metric: **Surface Area Heuristic (SAH)** — picks splits to minimize expected traversal cost = `Cₜᵣ + (Aᴸ/A) Σ cost(L) + (Aᴿ/A) Σ cost(R)`.
- Traversal: stackless or stack-based; modern GPUs prefer iterative w/ a short fixed stack.

## What Metal gives us (Metal 3+)

Metal exposes hardware-accelerated ray tracing on Apple9+ GPUs (M3 series). The relevant types live under `MTLAccelerationStructure*`:

- `MTLPrimitiveAccelerationStructureDescriptor` for triangles or AABBs.
- `MTLInstanceAccelerationStructureDescriptor` for 2-level BVH (instances over geometry).
- `MTLAccelerationStructureCommandEncoder` for build / compact / refit.
- `intersection_function_table` for any-hit / closest-hit / intersection shaders in MSL.
- `intersector<intersection_tags::...>` in MSL for traversal.

We do **not** need to write our own BVH — Metal owns construction quality and traversal cost. We just feed it geometry and animate instance transforms.

## What we get from PBRT anyway

- **Intuition** on cost: ray cost is ≈ traversal_cost · log₂ N + intersection_cost · expected_leaf_count. Use this to budget passes.
- **Refit vs rebuild**: refit (translate AABBs of existing nodes) is fast; quality degrades for non-rigid deformation. Rebuild from scratch for deformable / skinned meshes.
- **Compaction**: Metal's `MTLAccelerationStructureUsageRefit` + compact pass reclaims memory after building with refit-friendly layout.
- **Visibility patterns**: shadow rays (single-bit hit) traversal is cheap; reflections / GI bounce are not — budget accordingly.

## How this maps to our Phase 1.5 design

1. **Hybrid pipeline** (per ADR-0007): primary opaque is deferred raster; ray tracing pays for shadows + selectable reflections.
2. **TLAS / BLAS split**: per-mesh BLAS (rebuilt at load), per-frame TLAS (refit when instances move).
3. **Skinned meshes**: rebuild BLAS each frame (or skip, use proxy box). Budget item.
4. **Shaders**:
   - Shadow ray = visibility query → very thin closest-hit, no material eval.
   - Reflection ray = at least one bounce of shading; reuses lighting passes via intersector primitives.

## Open questions for Phase 1.5

- Use Metal's hardware ray tracing exclusively (Apple9+), or also support compute traversal fallback for Apple7/8?
- Acceleration structure storage as a separate frame-graph "imported" resource lane.
- Memory pool sizing — TLAS+BLAS can dwarf G-Buffer for dense scenes.

## See also

- PBRT Ch. 4 — full BVH/kd-tree theory.
- Apple sample "Ray Tracing in Metal" — concrete API usage.
- "Understanding the Efficiency of Ray Traversal on GPUs" — Aila & Laine 2009.
