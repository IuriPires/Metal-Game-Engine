# ADR-0008 — Compute pipelines as a first-class RHI surface

- **Status**: accepted
- **Date**: 2026-05-12
- **Deciders**: renderer
- **Tags**: rhi, compute, metal, phase-1.5

## Context

Phase 1 shipped a graphics-only RHI (vertex + fragment functions, render encoders). Phase 1.5 is anchored on workloads that need general-purpose GPU compute:

- GPU particle simulation (M12 deliverable)
- HZB / depth pyramid for GPU-driven occlusion culling
- Skinning compute shader for skeletal animation
- IBL prefilter chain (irradiance + prefiltered specular + BRDF LUT)
- Future: GPU-side culling, indirect draw generation, post-FX moved to compute

Continuing to expose graphics-only pipelines would force us to fake compute work as fragment passes over off-screen RGBA targets — wasteful on bandwidth, awkward for read-write storage buffers, and a dead end for anything that needs threadgroup memory or barriers.

## Decision

Add `ComputePipeline` and `ComputeEncoder` as peers of `RenderPipeline` / `RenderEncoder` in the RHI. `Device::create_compute_pipeline` mints them; `CommandBuffer::begin_compute_pass(label)` opens an encoder that can bind buffers, textures, samplers, and dispatch by threadgroup count or by total thread count.

A single `CommandBuffer` interleaves compute and render encoders freely; closing one before opening the next is the only constraint (matches Metal's encoder lifetime).

The FrameGraph stays texture-centric for v1. Compute work either:
- Lives inside a render pass's execute lambda (begin compute, dispatch, end, then begin render — pattern used by the M12 particle pass), or
- Becomes a standalone pass whose ordering relative to its consumers is established via the texture/backbuffer dependency they share (no first-class buffer handles yet).

First-class FrameGraph **buffer** resources are deferred (tracked as P1-FG-BUFFER-001 in TECH_DEBT.md).

## Alternatives considered

1. **Fragment-shader emulation of compute.** Write all "compute" work as fullscreen-quad fragment passes against R32F/RGBA32F render targets. Pros: zero new RHI surface area. Cons: no threadgroup memory, awkward state, terrible for HZB / parallel reductions, can't share storage buffers with vertex stage cleanly. **Rejected** — would have to be undone the moment we ship occlusion culling or skinning.

2. **Compute as a sub-mode of RenderEncoder.** Reuse `RenderEncoder` with a "compute" toggle. Pros: smaller API. Cons: muddies semantics (a render encoder is bound to a `RenderPassDescriptor` with attachments — compute has none), and fights the underlying `MTLComputeCommandEncoder` lifetime. **Rejected** — keep the encoders parallel, not unified.

3. **Skip the encoder wrapper, expose `MTL::ComputeCommandEncoder` directly.** Pros: maximum control. Cons: leaks Metal types past the RHI seam, blocks the Vulkan/DX12 backends we still want. **Rejected** — same reasoning as the M3 RHI ADR.

## Consequences

**Easy now:**
- Particle simulation, HZB construction, IBL prefilter chains, mip-reduce auto-exposure, GPU culling can all be authored as `kernel` functions.
- `ComputePipeline::thread_execution_width()` and `max_total_threads_per_threadgroup()` are exposed so callers can pick threadgroup sizes appropriately (matches MTL).

**Committed:**
- The RHI now has two pipeline kinds. Future backends (Vulkan, DX12) must implement both.
- Encoder pairs follow the same RAII pattern; the parent `CommandBuffer` is single-encoder-at-a-time.

**Explicitly not committing to:**
- Asynchronous compute on a separate queue (Phase 2+; Metal exposes it but we don't model multi-queue yet).
- First-class buffer resources in the FrameGraph (deferred).
- Indirect command buffers / argument buffers (separate ADR when we need them for GPU-driven rendering).
- Tile shaders / threadgroup memory cooperative APIs (when M13+ workloads demand them).

## Tradeoffs

- **API surface**: +1 pipeline type, +1 encoder type, +1 device factory, +1 command-buffer entry. Small, mirrors Render.
- **Dev time**: ~half a day to land the surface + Metal backend + unit/integration test. Net negative if we stay graphics-only, net positive within Phase 1.5 since every queued item benefits.
- **Runtime cost**: Zero overhead vs raw Metal — `void* native()` passthrough.

## Open questions

- When to add FrameGraph buffer handles? Likely when a compute pass produces a buffer that a *different* compute pass consumes (e.g., HZB chain). Until then, keeping compute work co-located with its consumer pass is fine.
- Async compute queue: needed before or after multi-threaded recording? Probably after, since the win is bigger when we have parallel CPU recording.

## Implementation notes

- `engine/renderer/rhi/include/mge/rhi/pipeline.h` — `ComputePipelineDesc`, `ComputePipeline`.
- `engine/renderer/rhi/include/mge/rhi/encoder.h` — `ComputeEncoder` (set_pipeline / set_buffer / set_texture / set_sampler / dispatch / dispatch_threads / end).
- `engine/renderer/rhi/include/mge/rhi/command_buffer.h` — `begin_compute_pass(label)`.
- `engine/renderer/rhi/include/mge/rhi/device.h` — `create_compute_pipeline`.
- `engine/renderer/metal/src/{pipeline,encoder,command_buffer,device}.cpp` — Metal implementations using `MTL::ComputePipelineState`, `MTL::ComputeCommandEncoder`, `dispatchThreadgroups` / `dispatchThreads`.
- `tests/integration/test_compute.cpp` — fills a `Shared` buffer via a kernel and validates the output on the CPU side.

## References

- ADR-0004 — RHI abstraction.
- Apple Metal: Compute Command Encoder (Metal Programming Guide, Compute Resources chapter).
- M12 — GPU particle system (first consumer; demo in `examples/hello_metal`).
