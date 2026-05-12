# Real-Time Rendering 4e — Chapter 2: The Graphics Rendering Pipeline

> Notes distilled for engine implementation. Always cross-reference the source for diagrams and precise terminology.

## Core idea

The rendering pipeline turns a 3D scene description into a 2D image. It is split into **application**, **geometry processing**, **rasterization**, and **pixel processing** stages, each of which can be subdivided into substages.

Throughput is dominated by whichever stage is the bottleneck — a triangle waiting in the post-clip queue is no different from a fragment waiting at a ROP. Engine code should be able to tell us *which* stage is the bottleneck at any moment. This drives M11 (profiling overlay) and the per-pass GPU timestamp design.

## Stages

### 1. Application
Runs on the CPU. Game logic, animation, AI, simulation, **culling**, command-buffer recording. This is the stage we directly control with code we write — the rest is GPU-side or driver-side.

Implication for our engine: the **frame graph** (M5) lives here, as does culling (M8) and command-list recording (M3).

### 2. Geometry processing
Vertex shading, projection, **clipping** (view frustum + user planes), **screen mapping**, optional tessellation, optional geometry shading.

We do most of this in vertex shaders. Tessellation and geometry shaders are skipped in Phase 1 (poor TBDR characteristics).

### 3. Rasterization
Triangle setup + triangle traversal. Determines which pixels are covered. The driver handles this; we feed it.

Implication: lots of tiny triangles are bad — fixed setup cost dominates. Hand in hand with LOD (Phase 1.5).

### 4. Pixel processing
Pixel shading, merging (depth/stencil test, blending). On Apple GPUs this is **tile-bound** (TBDR) — pixels are processed in tile-sized batches with on-tile depth/color.

Implication: programmable blending (read tile color) is available; consider for combining lighting with composite passes. This is exactly the win that makes single-pass G-Buffer → deferred lighting attractive on Apple (revisit at M9 / Phase 1.5).

## Bottleneck mental model

The book emphasizes that "the pipeline runs at the speed of its slowest stage". Practical heuristics:

| Symptom | Likely bottleneck |
|---------|-------------------|
| FPS drops as resolution rises | Pixel-bound |
| FPS drops as triangle count rises | Geometry-bound |
| FPS independent of either | CPU-bound (application) |
| Bandwidth-saturated | Memory-bound (G-Buffer, full-screen passes) |

Profile in both CPU (Tracy) and GPU (counter sample buffer) to disambiguate.

## Performance levers we have

1. **Cull aggressively** (M8): frustum first, then ideally HZB (Phase 1.5).
2. **Pack G-Buffer thinly** (M6): every channel of G-Buffer is bandwidth at every read.
3. **TAA over MSAA on deferred** (M9): MSAA is expensive on deferred. TAA + thin G-Buffer is the modern default.
4. **GPU-driven indirect** (M8): `MTLIndirectCommandBuffer` lets us batch state changes.
5. **Compute over fragment** when latency matters (M9, lighting + post): compute scheduling is more flexible.

## What this chapter implies for our docs

- `docs/RENDERING_PIPELINE.md` should label each pass with which pipeline stage it stresses.
- `docs/TESTING_STRATEGY.md` perf gates should split CPU and GPU budgets.
- `docs/MEMORY_MODEL.md` should emphasize bandwidth (not just allocation count).

## See also

- RTR4 Ch. 3 (GPU) — `research/rtr4-ch03-gpu.md` (TODO)
- Apple TBDR — `research/apple-tbdr-architecture.md`
