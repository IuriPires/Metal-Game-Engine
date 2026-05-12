# Architecture

System-of-systems view of the engine. Implementation detail lives in the per-subsystem docs.

## Goals

- **Correctness first**, then debuggability, then performance.
- **Data-oriented** layout in hot paths. Cache locality > inheritance ergonomics.
- **Explicit ownership** of resources. No hidden lifetimes.
- **Platform-abstracted** but only where there is at least one real second backend on the roadmap.
- **Profiler-native** — every subsystem ships with timing zones.

## Layering

```
                ┌──────────────────────────────────────────────┐
                │                 Game / Editor                │  ← Phase 2+
                └───────────────┬──────────────────────────────┘
                                │
                ┌───────────────▼──────────────────────────────┐
                │  Render System  ·  Sim System  ·  Asset Mgr  │
                └───────────────┬──────────────────────────────┘
                                │
                ┌───────────────▼──────────────────────────────┐
                │           Frame Graph (passes, DAG)          │
                └───────────────┬──────────────────────────────┘
                                │
                ┌───────────────▼──────────────────────────────┐
                │      RHI  (device, queue, cmdbuf, pso, …)    │
                └─────┬─────────────────┬─────────────────┬────┘
                      │                 │                 │
                ┌─────▼─────┐     ┌─────▼─────┐     ┌─────▼─────┐
                │   Metal   │     │  Vulkan*  │     │   DX12*   │   * future
                └───────────┘     └───────────┘     └───────────┘

   ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
   │     Core     │  │     Math     │  │   Memory     │  │     Jobs     │
   └──────────────┘  └──────────────┘  └──────────────┘  └──────────────┘
       Logger          vec/mat/quat       Arenas             Threadpool
       Assert          NEON SIMD          Pools              (no fibers
       Result          SoA-friendly       FrameRing           Phase 1)
       Time            MSL ABI            GPU heap
       Config

   ┌──────────────────────────────────────────────────────────────────────┐
   │                            Platform (macOS)                          │
   │           window · input · swapchain · clock · process               │
   └──────────────────────────────────────────────────────────────────────┘
```

## Subsystems

### Core (`engine/core/`)

- **Logger** — leveled, sink-extensible, thread-safe, no alloc in hot path.
- **Assert** — `MGE_ASSERT`, `MGE_VERIFY`, `MGE_UNREACHABLE` with stable line/file/expr capture and optional debugger break.
- **Result<T,E>** — sum type for fallible APIs. No exceptions in render code.
- **Time** — monotonic high-res clock, `mach_absolute_time`-backed on Apple. Returns `Duration` / `TimePoint` strong types.
- **Config** — typed TOML-style key-value store. JSON or TOML to be decided in ADR-0008 (deferred).

### Math (`engine/math/`)

- `vec2/3/4`, `mat3/4`, `quat`, `aabb`, `plane`, `frustum`.
- 16-byte aligned, SoA-friendly.
- NEON intrinsics on Apple Silicon, scalar fallback elsewhere.
- ABI compatible with MSL `packed_float3`, `float4x4`, etc. — verified with `static_assert`s on `sizeof`/`alignof` per ADR-0003.

### Memory (`engine/memory/`)

- **Arena** — bump allocator, reset per frame.
- **Pool** — fixed-size object allocator with free-list.
- **FrameAllocator** — ring of 3 arenas, indexed by frame-in-flight.
- **GpuHeap** — `MTLHeap` sub-allocator with placement tracking.
- All allocators publish stats consumed by overlay (M11) and Tracy.

### Platform (`engine/platform/macos/`)

- `Window` — Cocoa window via `NSWindow`. Phase 1 uses one `.mm` translation unit here only if needed; otherwise pure Metal-cpp + NSApp via objc runtime.
- `Input` — keyboard/mouse polling, edge-triggered events queue.
- `Swapchain` — `CAMetalLayer` + `CAMetalDrawable` management.
- `Clock` — wraps `mach_absolute_time`.

### Jobs (`engine/jobs/`)

Phase 1: minimal threadpool with stealable lock-free deques. No fibers, no priorities beyond hint flags. Phase 2 will replace with a proper work-stealing scheduler. See `docs/THREADING_MODEL.md`.

### Profile (`engine/profile/`)

- Tracy zones macros (`MGE_PROFILE_ZONE_*`).
- GPU timestamp query helpers — wraps Metal counter sample buffers.
- Stat counters consumed by overlay.

### Renderer (`engine/renderer/`)

See `docs/RENDERING_PIPELINE.md`. Layered as: high-level scene → frame graph → RHI → Metal backend.

### Assets (`engine/assets/`)

Phase 1: glTF mesh + PBR materials via cgltf, textures via stb_image. Cooked asset pipeline is Phase 2+.

### ECS (`engine/ecs/`)

Phase 1 stub: tagged component arrays per archetype, hand-written for the renderable / transform / camera/light components actually needed by M6-M9. Full ECS lands in Phase 2.

## Threading model (one-paragraph summary; full detail in THREADING_MODEL.md)

Main thread does window/input. A dedicated **simulation thread** runs the fixed-timestep accumulator. A dedicated **render thread** consumes interpolated state snapshots and records command buffers. A worker pool handles parallel culling, mip generation, and command-list fan-out. CPU may be up to 2 frames ahead of GPU (triple-buffered frame data).

## Data flow per frame

```
[ Input thread ] ──► InputQueue
                       │
                       ▼
[ Sim thread (60Hz) ]  ──► SimSnapshot[N]   ─┐
                       ▲                    │
                       │ accumulator        │
                       ▼                    │
                                            ▼
[ Render thread ] ──► interpolate(SimSnapshot[N-1], SimSnapshot[N], α)
                       ──► build FrameGraph
                       ──► record CommandBuffers via RHI
                       ──► submit to MTLCommandQueue
                                │
                                ▼
                        ┌───────────────┐
                        │      GPU      │  ──► CAMetalDrawable.present(at: t)
                        └───────────────┘
```

## Cross-cutting concerns

- **Logging**: every subsystem owns a `LogCategory` constant.
- **Profiling**: zones at every milestone boundary (sim step, frame graph compile, pass, submit, present).
- **Assertions**: liberal in Debug; preserved as `MGE_VERIFY` in Release for invariants that cannot be removed.
- **Determinism**: only the sim thread is asked to be deterministic. Render does not affect game state.

## Future hooks (not implemented Phase 1, but design-aware)

- Async compute queue.
- Bindless argument buffers Tier 2 (used from M6, but minimally).
- Ray tracing acceleration structures (Phase 1.5).
- Multi-viewport / multi-window (editor support, Phase 2).
- Live shader reload + hot reload of C++ via blueprint scripts (Phase 3).

## See also

- [RENDERING_PIPELINE.md](RENDERING_PIPELINE.md)
- [GAME_LOOP.md](GAME_LOOP.md)
- [MEMORY_MODEL.md](MEMORY_MODEL.md)
- [THREADING_MODEL.md](THREADING_MODEL.md)
- [TESTING_STRATEGY.md](TESTING_STRATEGY.md)
