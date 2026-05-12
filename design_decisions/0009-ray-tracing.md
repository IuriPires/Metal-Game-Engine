# ADR-0009 — Inline ray queries over a static TLAS (hybrid RT)

- **Status**: accepted
- **Date**: 2026-05-12
- **Deciders**: renderer
- **Tags**: rhi, ray-tracing, metal, phase-1.5

## Context

Phase 1.5 calls for hybrid ray tracing: rasterize the G-Buffer as today, then ray-trace shadows and reflections in the lighting pass. Apple silicon (M1+) supports ray tracing — both ray-tracing pipelines with shader binding tables, and **inline ray queries** invokable from any function stage (vertex/fragment/compute).

Two big design forks needed answers:

1. **Ray tracing pipeline vs inline ray queries.** A full RT pipeline (closest-hit / any-hit / miss programs + shader binding table) gives shading flexibility per hit, at the cost of a separate pipeline kind, shader tables, and resource binding plumbing. Inline ray queries (`intersection_query<...>` in MSL) live inside an existing fragment shader and only need the TLAS bound as one extra resource.
2. **Per-frame TLAS rebuild vs static TLAS.** The hello_metal scene has 1k+ cubes (static), 5 spheres (rotating in M11), and a ground plane (static). A per-frame rebuild costs ~real time at this instance count even with blocking builds. A fully static TLAS forces the scene to be static.

## Decision

- **Inline ray queries.** The lighting fragment shader takes the TLAS via `instance_acceleration_structure tlas [[buffer(N)]]` and uses `intersector<instancing>` to fire shadow and reflection rays. No ray-tracing pipeline, no shader binding table.
- **Static TLAS for M13.** Built once at startup over all spheres, the ground, and every cube. The sphere wobble from M11 is removed so the TLAS stays correct (spheres are rotationally symmetric, so static positions still match the rendered geometry under M11's around-origin rotation only when the rotation is dropped).
- **RT shadows replace CSM in the lighting shader** when the device supports ray tracing from render. The CSM shadow pass still runs (its output is unused), and `--no-rt` flips the demo back to the CSM-sampling lighting PSO. The shadow pass + map will be removed once we no longer need a fallback.
- **RT reflections for metallic surfaces only.** One reflection ray per metallic pixel. Hit → approximate as `albedo * sun_visibility * NoL + ambient`; miss → sky gradient aligned to the sun. Strength is mixed against the existing PBR specular by `metallic * shadow_params.z`.

## Alternatives considered

1. **Full RT pipeline with closest-hit programs.** Cleaner per-hit shading — each material can supply its own hit shader. Pros: real reflections of arbitrary geometry. Cons: shader binding tables, separate pipeline kind, much more API surface, slow iteration. Net: not worth it for a vertical slice that already validates RT works through the rasterized path. Re-open if/when we want material-correct reflections.

2. **Per-frame TLAS rebuild.** Would let us keep the sphere wobble (and animated geometry in general). Pros: dynamic scenes. Cons: today's blocking build helper would stall a frame on CPU each rebuild; the better path is a non-blocking accel-structure command encoder driven by the FrameGraph. Deferred to M13.b.

3. **Move shadows entirely to RT, delete CSM.** Cleaner. Cons: kills the only working multi-megabyte shadow-mapping pipeline in the engine and forces RT-only hardware. We keep CSM behind `--no-rt` for now; the shadow pass becomes vestigial when RT is on (tracked in TECH_DEBT).

## Consequences

**Easy now:**
- Crisp pixel-perfect shadows with no shadow-map artifacts (no acne, no swimming, no PCF resolution tradeoff). Tens of thousands of instances cast shadows for free.
- Metallic spheres / cubes reflect their surroundings against the actual scene geometry.
- Sky becomes a real gradient sampled at the same place rays land — no more "discard → cleared HDR" hole that produced the pure-black sky pre-M13.

**Committed:**
- Lighting shader and BLAS/TLAS exist as first-class concepts. Future deferred passes that want shadows will use the TLAS.
- One extra dependency: every `BLAS` that the bound TLAS references must be marked via `useFragmentAccelerationStructure` so Metal keeps it resident across the encoder.

**Explicitly not committing to:**
- Animated TLAS / per-frame refit (M13.b).
- Material-aware closest-hit shading (M13.c, may switch to a full RT pipeline).
- Multi-bounce reflections (single bounce only).
- Ray-traced GI (separate milestone class).

## Tradeoffs

- **GPU cost**: roughly +2–3 ms at 1080p with the M11 scene on M1 Pro (still 120 FPS-locked here since the frame headroom was ~3 ms before). Acceptable.
- **CPU cost**: TLAS build runs once at startup; ~ms.
- **Memory**: each BLAS is small (few KB), TLAS over 1018 instances is well under 1 MB.
- **API surface**: +1 RHI type (`AccelerationStructure`), +2 desc structs, +2 `Device` factory methods, +2 `RenderEncoder` methods (`set_fragment_acceleration_structure`, `use_fragment_acceleration_structure`).

## Open questions

- When to add an `AccelStructEncoder` and FrameGraph integration for incremental TLAS rebuild? Probably with M13.b once we want animated scenes.
- Refit vs rebuild — Metal supports both. Refit is much cheaper, valid as long as topology doesn't change.
- Closest-hit programs: do we ever need them, or is "sample G-Buffer at the secondary-hit footprint" enough for our use cases?

## Implementation notes

- `engine/renderer/rhi/include/mge/rhi/acceleration_structure.h` — `TriangleGeometryDesc`, `PrimitiveAccelDesc`, `AccelInstance`, `InstanceAccelDesc`, `AccelerationStructure`.
- `engine/renderer/rhi/include/mge/rhi/device.h` — `build_acceleration_structure(Queue&, PrimitiveAccelDesc)` + `build_acceleration_structure(Queue&, InstanceAccelDesc)` blocking helpers. `DeviceInfo::supports_ray_tracing_from_render` exposes `supportsRaytracingFromRender`.
- `engine/renderer/metal/src/acceleration_structure.cpp` — descriptor build, `accelerationStructureSizes`, `newAccelerationStructure(size)`, scratch buffer, `accelerationStructureCommandEncoder()::buildAccelerationStructure`, commit+wait.
- `engine/renderer/rhi/include/mge/rhi/encoder.h` — `set_fragment_acceleration_structure(as, slot)` + `use_fragment_acceleration_structure(as)`.
- `tests/integration/test_raytrace.cpp` — fires a fragment-stage ray query against a one-triangle BVH and asserts hit/miss patterns.
- `examples/hello_metal/main.cpp` — `lighting_rt_*` MSL, `--no-rt` flag, RT lighting PSO, BLAS for sphere/cube/ground, scene TLAS over 1k instances.

## References

- Apple Metal: Acceleration Structures Guide; Inline Ray Queries section.
- ADR-0004 — RHI abstraction (justifies adding new RHI types instead of leaking MTL types).
- ADR-0008 — Compute pipelines (same "minimal API surface" disposition).
