# Rendering Pipeline

## Goals

- Deferred PBR primary path + Forward+ transparency.
- 120 FPS on M-series with a "Sponza-class" scene by end of Phase 1.
- Frame graph as the single source of truth for resource lifetimes and barriers.
- Single-backend today (Metal), pluggable tomorrow.
- Every pass profileable and skippable for debugging.

## Phase 1 pipeline (sketch)

```
[ Cull (CPU, NEON SoA) ]
        │
        ▼
[ Shadow Pass ] ─────────────► CSM atlas (4 cascades)
        │
        ▼
[ GBuffer ]
    ├─ Albedo+AO        (RGBA8)
    ├─ Normal+Roughness (RG16F+R8+R8)   ← octahedral normal
    ├─ F0+Metallic      (RGBA8)
    ├─ MotionVectors    (RG16F)
    └─ Depth            (D32F)
        │
        ▼
[ Cluster light cull (compute) ]
        │
        ▼
[ Lighting (deferred, fullscreen) ] ─── reads CSM, IBL, GBuffer
        │
        ▼
[ Forward+ transparent pass ] ── reads cluster grid + lighting LUTs
        │
        ▼
[ Post chain ]
    ├─ Auto-exposure (histogram, compute)
    ├─ Bloom (down/up pyramid)
    ├─ Motion blur (Phase 1.5)
    ├─ TAA (with neighborhood clamp)
    └─ Tonemap (ACES) → present
        │
        ▼
[ Particles (M12) ]
    ├─ Compute step: gravity / integration / life decay / respawn
    └─ Additive billboard render over tonemapped scene
        │
        ▼
[ UI / overlay ]
        │
        ▼
[ Present (CAMetalDrawable) ]
```

## RHI

A thin C++ abstraction. Names approximate Vulkan/D3D12 terminology — Metal maps onto these cleanly.

| RHI concept | Metal mapping |
|-------------|---------------|
| `Device` | `MTLDevice` |
| `Queue` | `MTLCommandQueue` |
| `CommandBuffer` | `MTLCommandBuffer` |
| `CommandEncoder` (graphics) | `MTLRenderCommandEncoder` |
| `CommandEncoder` (compute) | `MTLComputeCommandEncoder` |
| `CommandEncoder` (blit) | `MTLBlitCommandEncoder` |
| `Buffer` | `MTLBuffer` (with placement on `MTLHeap`) |
| `Texture` | `MTLTexture` (with placement on `MTLHeap`) |
| `Sampler` | `MTLSamplerState` |
| `Pipeline` (graphics) | `MTLRenderPipelineState` |
| `Pipeline` (compute) | `MTLComputePipelineState` |
| `BindGroup` | Argument buffer Tier 2 |
| `Fence` | `MTLSharedEvent` |
| `Query` | counter sample buffer |

**Why an abstraction now**: forces the renderer to express intent, mockable for unit tests, prevents Metal idioms from leaking into pass code. Cost is real but small at the granularity used (per command list, not per draw).

See ADR-0004.

## Frame Graph

Pass = function with declared inputs and outputs. Resources are *virtual*; physical allocation happens once per frame.

```cpp
// Pseudocode shape — exact API designed in M5.
graph.add_pass("GBuffer", [&](Builder& b) {
    b.write(albedo_ao, TextureUsage::ColorAttachment, Format::RGBA8Unorm);
    b.write(normal_rough, TextureUsage::ColorAttachment, Format::RGBA8Snorm);
    b.write(motion, TextureUsage::ColorAttachment, Format::RG16Float);
    b.write(depth, TextureUsage::DepthAttachment, Format::Depth32Float);
    return [=](RenderContext& ctx) {
        ctx.set_pipeline(pso.gbuffer);
        ctx.draw_indexed_indirect(cmd_buffer.gbuffer);
    };
});
```

- Resources can alias if their virtual lifetimes don't overlap.
- Barriers / transitions are derived from declared usage.
- Topological sort + per-pass timestamps for profiling.
- Debug: dump to Graphviz `.dot`.

Phase 1 deliberately ships v1 only: no async compute, no split barriers, no subpasses. Those land post-M9.

See ADR-0005.

## Shader System

- MSL is canonical. `.metal` source compiled to `.metallib` offline in Release, JIT in Debug for hot-reload.
- Shared header convention: `shaders/metal/common/*.h` are includable from both MSL and C++ — only ABI-stable types (no method bodies).
- Reflection: parse `MTLArgumentEncoder` info to discover binding indices; cached per pipeline.
- Hot reload: file watcher in Debug recompiles on save; falls back to last-good pipeline on error.

## PBR

- Microfacet BRDF: GGX-Smith with correlated visibility.
- Fresnel: Schlick.
- Multiscattering compensation (Fdez-Agüera).
- IBL: split-sum approximation — irradiance map for diffuse, prefiltered specular pyramid for indirect, BRDF LUT (2D, RG16F).
- Color: linear scene-referred; tonemap at the very end.

Validation: furnace test, white-furnace energy conservation, reference comparison vs. existing PBR viewers.

## Shadows

- Cascaded Shadow Maps for the sun light, 4 cascades, logarithmic + uniform hybrid splits.
- Stable cascade: snap cascade origin to texel grid to avoid swimming.
- Filtering: PCF 3×3 baseline; PCSS as a Phase 1.5 stretch.

## Post FX

- Auto-exposure via luminance histogram (compute).
- Bloom: progressive down/up pyramid (~6 mips), Karis average on first downsample to reduce fireflies.
- TAA: history buffer, motion-vector reprojection, neighborhood clamp (min/max box of 3×3), anti-flicker by clamping in YCoCg.
- Tonemap: ACES RRT+ODT approximation (Stephen Hill `ACESFitted`).

## Culling & instancing

- Frustum cull on CPU with SoA AABB lanes (NEON `float32x4` fan-out).
- Visibility result feeds a per-frame visible-instance buffer used by indirect draws (`MTLIndirectCommandBuffer`).
- GPU occlusion culling is Phase 1.5.

## Color & precision

- Internal HDR: `RGBA16Float`.
- LDR present target: `BGRA8Unorm_sRGB` (or `RGB10A2Unorm` if HDR display is detected, Phase 1.5).
- Depth: `Depth32Float`, reverse-Z.

## Per-pass GPU budget targets (M-series, 1440p, end of Phase 1)

These are budgets, not measurements. Profiling lands at M11.

| Pass | Budget (ms) |
|------|-------------|
| Cull (CPU) | 0.5 |
| Shadow cascades (×4) | 0.8 |
| GBuffer | 1.5 |
| Cluster light cull | 0.3 |
| Lighting (deferred) | 1.2 |
| Forward+ transparent | 0.6 |
| Bloom | 0.4 |
| TAA + tonemap | 0.5 |
| Other / overhead | 0.5 |
| **Total** | **~6.3** (target 8.3 ms = 120 FPS) |

## Validation / tests

- Unit: math, frame graph topology, mock RHI verifying resource transitions.
- Integration: render-a-triangle, render-a-cube via RHI; render-a-scene via full pipeline.
- Golden image: per-pass and final output PNGs with perceptual diff tolerance.
- Perf: headless scene at fixed seed, fail CI if frame budget regresses.

## See also

- ADR-0004 (RHI), ADR-0005 (frame graph), ADR-0007 (deferred primary path)
- `research/rtr4-ch09-pbr.md`
- `research/apple-tbdr-architecture.md`
