# ADR-0010 — HZB occlusion culling (single-resolution, stats-only v1)

- **Status**: accepted
- **Date**: 2026-05-12
- **Deciders**: renderer
- **Tags**: rhi, compute, culling, phase-1.5

## Context

M8 introduced CPU frustum culling: the cube field is tested against the camera frustum per-frame on the CPU (NEON 4-wide SoA), producing a packed visible-instance list that drives a single instanced draw. Frustum culling cuts everything outside the view cone, but cubes occluded by other cubes (a common case in any dense scene) still rasterize.

Hierarchical Z-Buffer (HZB) occlusion culling is the standard fix: build a depth pyramid from the current frame's depth buffer, then per-instance project the world AABB into screen space and compare its nearest depth against the HZB sample under its rect. If the AABB's near point is **farther** than the HZB's MAX depth in the same screen region, the instance is fully occluded.

Two big scope forks:

1. **Mipmap pyramid vs single-resolution HZB.** A proper pyramid samples the mip level whose texel size matches the AABB's screen rect (cheap, O(1) per instance). A single-resolution HZB needs to max-reduce the full texel rect each query (O(area), but trivial to implement and no mip-binding plumbing).
2. **Real cull (skip draws) vs stats-only.** Real cull means feeding cull results back into the draw call. Cleanest path is GPU-driven via `MTLIndirectCommandBuffer`, which is a sizable separate body of work. Stats-only proves the pipeline works and lays groundwork.

## Decision

M14 v1 ships:

- **Single-resolution HZB** at 256×256, format R32Float, transient FrameGraph texture. Built by a compute kernel that max-reduces the gbuffer Depth32Float into the HZB, one HZB texel per thread.
- **Stats-only HZB cull**. A second compute kernel iterates the visible cube subset (input = CPU frustum-culled list, already packed in `instance_buf` starting at `cube_base`), projects each instance's local unit AABB through `view_proj`, computes the screen-space rect, max-reduces the HZB texels under it, and compares to the AABB's min NDC z. Writes a per-instance visibility byte and bumps an atomic counter of occluded cubes. The counter (Shared-storage buffer) is read back on CPU one frame later and displayed in the overlay HUD.
- **No actual draw filtering** yet. The cube vertex shader does not consult the visibility bytes. Phase 1.5+ will wire that in once we have either an `MTLIndirectCommandBuffer` path or vertex-shader degenerate-clipping.

`ResourceUsage::ShaderWrite` was added to the FrameGraph handle enum so compute writes mark the texture distinctly from color attachments.

## Alternatives considered

1. **Proper mip pyramid (cheap-per-instance).** Each subsequent mip = max(2×2) of the previous. Per-instance, pick the mip whose texel ≥ AABB rect, do a single 2×2 sample. Pros: O(1) per instance, no max-rect loop. Cons: needs texture views for per-mip read/write in compute (Metal can read from a mip via `read(coord, level)` but not write), or per-mip single-mip texture allocations, or render-pass mip-reduce. Net: more API surface. Punted to M14.b.

2. **CPU-side HZB build (read back depth).** Read the depth buffer back to CPU each frame, build the pyramid on CPU, push back to GPU. Pros: simpler in some ways. Cons: GPU→CPU readback latency, defeats the point. **Rejected.**

3. **Actual cull via GPU-driven indirect.** Cull writes an `MTLIndirectCommandBuffer`; the draw consumes it. Pros: real perf win. Cons: a whole new RHI surface (indirect command buffers, argument buffers). Lands as M14.b.

4. **Vertex-shader degenerate-clip.** Cull writes per-instance visibility byte; vertex shader reads it and outputs a degenerate triangle if 0. Pros: minimal API additions. Cons: still pays vertex shader cost; only fragment work is saved. Lands as M14.b alongside or before the indirect path.

## Consequences

**Easy now:**
- HUD reports HZB visible/occluded counts, validating that the cull pipeline produces real numbers on a real depth buffer.
- HZB build + stats compute passes integrate cleanly with the existing FrameGraph (compute encoders inside `add_pass` execute lambdas, same as M12 particles).
- `ResourceUsage::ShaderWrite` is now available for any future compute-write resource.

**Committed:**
- A 256×256 R32Float HZB allocates ~256 KB per frame transiently. Negligible.
- The HZB is conservative (MAX-reduce); occluded means **definitely** occluded.

**Explicitly not committing to:**
- Per-instance mip selection / proper mip pyramid (M14.b).
- Actual draw filtering (M14.b).
- Compute-shader writes to mip levels via texture views (not in RHI yet).

## Tradeoffs

- **GPU cost (M14 v1)**: HZB build is ~256×256 = 65 536 threads each reading ~30 source texels = ~2M reads. Trivial (< 0.1 ms on M1 Pro at 1080p source). HZB stats is one thread per visible cube (~300), each reading a few hundred HZB texels at most. Also negligible.
- **Memory**: +256 KB HZB transient + ~1 KB Shared buffer for the counter + ~1 KB Shared visibility buffer (per cube). Tiny.
- **API surface**: zero new RHI types; only `ResourceUsage::ShaderWrite` added at the FG layer.
- **What we DON'T save yet**: actual draw cost. M14.b unlocks that.

## Open questions

- Mip pyramid: build into the same texture with multiple `newTextureView` mip views, or N separate single-mip textures? The first is closer to standard practice; the second is simpler with our current RHI.
- Stats readback: today's CPU readback uses a Shared buffer with implicit Metal sync (read it next frame, after the previous frame's command buffer has committed). For latency-sensitive readbacks we'd want explicit completion handlers or sentinels.

## Implementation notes

- `engine/renderer/frame_graph/include/mge/frame_graph/handles.h` — `ResourceUsage::ShaderWrite`.
- `examples/hello_metal/main.cpp` —
  - `k_hzb_build_msl` (depth → HZB max-reduce, one thread per HZB texel).
  - `k_hzb_stats_msl` (per-cube AABB projection + HZB max-rect test + atomic counter).
  - `hzb_build_pso`, `hzb_stats_pso` compute pipelines.
  - `hzb_tex` FG transient texture (256² R32Float, ShaderRead+ShaderWrite).
  - HUD line `HZB %5u VIS %5u OCC` from the previous frame's counter.
- `tests/integration/test_hzb.cpp` — fills a 4×4 R32Float source with a known gradient via a fragment pass, runs a 2×2 reduce, validates tile-max math.

## References

- ADR-0008 — Compute pipelines (M12 cornerstone; M14 inherits the compute surface).
- ADR-0005 — Frame graph (where HZB build / cull passes plug in).
- Karis, "Graphics Gems for Games" SIGGRAPH 2014 — overview of HZB-based occlusion approaches in modern game engines.
- O'Donnell, "Tiled Hardware Occlusion Queries Made Useful" — explains why pyramid-based culling beats individual queries.
