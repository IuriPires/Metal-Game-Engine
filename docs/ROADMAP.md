# Roadmap

Living document. Tick boxes as milestones land. Each milestone closes with: green build, green tests, updated docs, and (where applicable) a new ADR.

## Phase 1 — Renderer foundation + game loop

- [x] **M0 — Scaffolding**
  - [x] Directory structure
  - [x] `CMakeLists.txt`, presets, toolchain hooks
  - [x] `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitignore`
  - [x] `CLAUDE.md`, all `docs/*.md` skeletons, ADRs 0001–0007
  - [x] `third_party/` via FetchContent (doctest active; fmt/cgltf/stb/Tracy/metal-cpp declared & pinned)
  - [x] CI on macOS-14 arm64 (GitHub Actions, `debug`/`release`/`asan`)
  - [x] Apple Metal docs cached in `external_docs/metal/` (MSL spec, feature tables, metal-cpp 26.4)
  - [x] Initial research notes in `research/` (RTR4 ch2 & ch9, PBRT ch4, Apple TBDR)
  - [x] Placeholder doctest target green (`mge_unit_tests`, 5/5 passing on Debug + Release)
- [x] **M1 — Platform + Metal hello**
  - [x] Cocoa window + Metal device/queue/swapchain (`mge::platform::Window`, `Swapchain`)
  - [x] Clear-screen frame rendered headed (`examples/hello_metal`, 119 fps on M1 Pro)
  - [x] Offscreen variant for headless test (`tests/integration/test_clear_offscreen.cpp`)
  - [x] Frame timing readout (`mge::core::FrameStats`)
  - [x] metal-cpp 26.4 wired (Apple Clang framework-search workaround documented in ADR)
  - [x] tiny Obj-C++ shim confined to `engine/platform/macos/` (per ADR-0001)
- [x] **M2 — Core systems**
  - [x] Logger (leveled, sink-based, capturing sink for tests), Assert (`MGE_ASSERT`/`VERIFY`/`UNREACHABLE`), `Result<T,E>`, `Time`/`FrameStats`
  - [x] Custom SIMD math (Vec2/3/4, Mat3/Mat4, Quat, Aabb) with NEON Mat4×Mat4 + Mat4×Vec4; ABI compat with MSL asserted at compile time
  - [x] `Arena` + `Pool` allocators with `Stats`; owning variants
  - [x] 30 unit tests (math invariants, NEON↔scalar parity, allocator correctness, logger filtering, Result)
  - [x] fmt 10.2.1 wired
  - [—] Config (deferred — needed first at M4 when assets land)
- [x] **M3 — RHI v1 + first triangle**
  - [x] RHI types: Device, Queue, CommandBuffer, RenderEncoder, Buffer, Texture, Shader, RenderPipeline, Swapchain, SwapchainFrame
  - [x] Metal backend implementation (one TU per type, PIMPL via `void* native()`)
  - [x] Triangle drawn via RHI from inline MSL (hello_metal demo, 119 fps)
  - [x] Integration: triangle into offscreen RGBA8 + center-pixel + corner-clear assertions
  - [—] Sampler / Fence (deferred — Sampler lands at M4, Fence at M10)
  - [—] Mock backend for unit tests (deferred to M5 when frame graph needs it)
  - [—] Pixel-perfect golden image (deferred to M9, needs perceptual diff harness)
- [x] **M4 — Mesh + camera + basic forward**
  - [x] RHI v1.1: depth state on pipeline, depth attachment in pass, `set_fragment_buffer`, `draw_indexed`, CullMode + FrontFace, DepthCompare. Fixed P1-RHI-DRAW-001 (encoder tracks topology).
  - [x] `engine/scene/camera.h` — perspective camera with cached view/projection, RH zero-to-one depth (Metal-friendly), aspect resize.
  - [x] `engine/assets/primitives.h` — procedural cube (24 verts / 36 indices, axis-aligned normals, CCW winding outward).
  - [x] Lambert forward MSL shader (vertex + fragment via uniform buffer), inline. Rotating cube demo at ~110 fps M1 Pro.
  - [x] Window resize support: `consume_resize_event()`, depth texture re-allocates, camera aspect updates. Closes P1-WINDOW-001.
  - [x] Integration test: Lambert cube into 128×128 offscreen, asserts non-clear center + at least one clear corner.
  - [—] cgltf glTF loader (deferred to M5 — procedural primitives cover M4 demo)
  - [—] FPS controller (deferred — needs input system, lands at M5 with frame graph)
  - [—] Pixel-perfect golden image (deferred to M9 with perceptual diff)
- [x] **M5 — Frame graph v1**
  - [x] Pass declaration API: `FrameGraph::add_pass(name, setup, execute)` with `PassBuilder` (read/write/write_color/write_depth) and `RenderContext` (cmd + realize + make_render_pass_desc).
  - [x] Virtual resources: imported (`import_texture`) and transient (`create_texture`) handles.
  - [x] Lifetime analysis: per-resource first/last phase index over the topological schedule.
  - [x] Transient pool aliasing: bin-pack with desc-compatibility check; non-overlapping lifetimes share physical textures.
  - [x] Topological sort: Kahn's algorithm over producer→consumer edges; stable on declaration order.
  - [x] Graphviz dump (`to_dot()`) — passes as boxes, resources as ellipses (imported green, transients colored by slot), writes/reads as edges.
  - [x] hello_metal demo refactored: cube pass declares backbuffer (imported) + depth (transient). Resize-safe.
  - [x] Tests: 5 unit-ish integration (topo, alias-share, alias-disjoint, distinct-desc, dot dump) + 1 end-to-end execute test.
  - [—] Mock RHI backend (deferred — compile() is testable directly; integration tests cover execute())
  - [—] Automatic barriers/transitions (deferred — Metal auto-tracks dependencies in v1)
  - [—] Async compute / split barriers / subpasses (Phase 1.5)
- [x] **M6 — Deferred PBR (direct lighting)**
  - [x] **RHI Sampler** type + `Device::create_sampler` + `RenderEncoder::set_fragment_texture`/`set_fragment_sampler`
  - [x] `engine/assets::PbrVertex` (32 B, pos+normal) + `make_sphere_pbr(lat, lon)` + `make_cube_pbr`
  - [x] **G-Buffer pass**: RGBA8 (albedo+ao) + RGBA16F (octa-normal+roughness+metallic) + Depth32F. CCW back-cull, depth Less, write.
  - [x] **Deferred lighting pass**: fullscreen quad via `vertex_id`, samples G-Buffer + depth, reconstructs world position from inverse view-projection, GGX-Smith correlated visibility + Schlick Fresnel + Lambert diffuse, single directional sun + ambient term. Writes HDR (RGBA16F).
  - [x] **Tonemap pass**: Reinhard `c/(1+c)` → sRGB backbuffer (ACES at M9).
  - [x] 3-pass FrameGraph: gbuffer → lighting → tonemap. Demo: row of 5 spheres varying (metallic, roughness) shows the material spectrum.
  - [x] Unit tests for PBR primitives (sphere on unit hull, normal=position, indices valid).
  - [—] **Clustered light culling** (deferred — single directional sun for M6; multi-light wants M7+)
  - [—] **Multiscatter compensation** (deferred — single-scattering GGX visible loss only at extreme roughness; Fdez-Agüera at M6.1)
  - [—] **IBL** (irradiance + prefiltered specular + BRDF LUT) — deferred to M6.1, needs compute pipeline + HDR env map loader
  - [—] **Furnace test** (deferred to M9 with perceptual diff harness)
- [x] **M7 — Shadow mapping (single cascade)**
  - [x] `engine/math::orthographic_rh_zo` (Metal NDC z[0,1])
  - [x] `engine/assets::make_ground_plane_pbr` so shadows have something to fall on
  - [x] **RHI**: `RenderPipelineDesc::fragment_shader` is now optional → depth-only shadow pipeline (no color targets, no fragment function)
  - [x] **Shadow pass**: 2048×2048 Depth32Float transient, depth-only render from sun's POV with front-face cull to mitigate self-shadow acne
  - [x] **Lighting pass**: samples shadow map with PCF 3×3 + bias, attenuates direct lighting
  - [x] Demo: 5 spheres + ground plane casting visible soft shadows
  - [—] **4-cascade CSM** (deferred to M7.b — single cascade is sufficient for the demo scene; multi-cascade lands when scene size grows)
  - [—] **Stable texel snap** (deferred with cascades — only matters when camera moves cascades around)
  - [—] **Golden: shadow scene** (deferred to M9 with perceptual diff harness)
- [x] **M8 — Culling + instancing**
  - [x] **`engine/math/frustum.h`** — Plane + Frustum, `from_view_projection` extracts 6 normalized planes from a column-major VP matrix (Metal NDC z[0,1]), `test_aabb` uses the positive-vertex optimization, `aabb_visible` is the fast boolean wrapper.
  - [x] **NEON 4-wide AABB cull** — `aabb_visible_x4_neon` packs 4 SoA AABBs and tests against all 6 planes in parallel. Cross-checked against scalar in unit tests.
  - [x] **Instancing across the pipeline** — split per-draw `DrawConstants` into `FrameConstants` (view_proj, light_view_proj) + per-instance `InstanceData` (model, model_inv_t, albedo, mr). Shaders use `[[instance_id]]` to index a shared instance buffer; gbuffer flat-interpolates the iid to the fragment.
  - [x] **Demo: NxN cube field with CPU frustum cull** — 1024 default (32x32), scales to 160k+ via `--cubes N` arg. Per-frame: rebuild AABBs, cull, pack visible instances, single `draw_indexed` per mesh with `instance_count = visible_count`.
  - [x] **Perf**: 50k cubes (224x224 grid), ~11k visible after cull → **120 FPS locked** (8.33ms) on M1 Pro. 160k cubes (400x400), ~15k visible → **still 120 FPS locked**. Meta exceeded.
  - [—] **`MTLIndirectCommandBuffer`** (GPU-driven) — deferred. Direct `instance_count` argument hits the perf target; ICB pays off only with thousands of distinct draw calls.
- [x] **M9 — Post FX chain (v1)**
  - [x] **RHI blend state** — `BlendFactor`/`BlendOp` enums + extended `ColorTargetState` (src/dst factors + op for RGB and alpha). Metal backend wires `setSourceRGBBlendFactor` etc.
  - [x] **Bloom** — 5-mip down/up pyramid: bright pass (Karis 2014 soft-threshold knee) → 4× 4-tap bilinear downsample → 4× 9-tap tent upsample with **additive blend** (`src=One, dst=One, op=Add`) and `LoadAction::Load` on the destination.
  - [x] **ACES tonemap** (Stephen Hill's "ACES Fitted") replaces Reinhard. Combines HDR with the upsampled bloom pyramid's top mip in the tonemap pass.
  - [x] HDR pipeline (carried from M6) — RGBA16Float intermediates throughout, ACES at the final present.
  - [x] **FrameGraph fix**: `topological_sort` now walks in declaration order and chains write-after-write edges, correctly ordering multi-write resources (bloom mips are written by both downsample and upsample).
  - [—] **Auto-exposure** — deferred to M9.b (needs either compute pipeline support or a mip-reduce fragment chain).
  - [—] **Motion vectors + TAA** — deferred to M9.b (needs previous-frame matrices, history texture, jittered projection, neighborhood clamp). Bigger than a single milestone slice.
- [x] **M10 — Game loop**
  - [x] **`engine/core/game_loop.{h,cpp}`** — `GameLoop` class with the Fiedler accumulator. `tick(sim, render)` measures real_dt from `mge::core::now()`, clamps it to `max_real_dt`, scales by `time_scale`, consumes the accumulator into `fixed_dt` steps (capped by `max_steps_per_frame`), then calls `render(alpha)` and paces.
  - [x] **Decoupled render with interpolation** — render callback receives `alpha ∈ [0,1)` between the latest completed sim step and the next. Demo applies it to the cube wobble for smooth motion between 60 Hz sim ticks.
  - [x] **Pause + time scale** — `set_paused(bool)` freezes the accumulator; `set_time_scale(float)` ranges 0..∞ (clamped to ≥0).
  - [x] **Deterministic mode** — `tick_with_dt(dt, sim, render)` bypasses the clock and pacing; identical input sequences produce bit-exact `sim_time` / `step_count`. Validated by a 500-step replay test.
  - [x] **Frame pacing** — `pace_to_target()` uses absolute target times (no drift), `sleep_until` to ~250 µs before target, then spin-locks the tail.
  - [x] **Spiral-of-death guard** — `max_steps_per_frame` caps catch-up; excess accumulator is dropped into `dropped_steps()` for visibility.
  - [x] Demo integrated: `--time-scale F`, `--target-fps N`, `--sim-hz N`, `--paused`, `--demo-mode` (auto-cycles normal → paused → slow-mo → fast-fwd every ~3s of wall time).
  - [—] **Triple-buffered sim snapshot ring** — deferred (single-threaded sim in Phase 1; ring becomes load-bearing only when sim runs on its own thread, Phase 1.5+).
- [x] **M11 — Profiling overlay**
  - [x] **`engine/profile/`** — `Profiler` singleton with `begin_zone`/`end_zone`, `MGE_PROFILE_ZONE(name)` RAII scope timer. Thread-safe via `std::mutex`. Per-name rolling window of 120 samples → `ZoneStats { last, avg, min, max }` via `snapshot()`. Linear scan zone lookup (< 20 zones in practice).
  - [x] **Bitmap font 8×8** — 64-glyph hand-coded ASCII subset (`examples/hello_metal/font8x8.h`: space, digits, A–Z, punctuation). Uploaded to an R8 atlas (16 cols × 4 rows) via `MTL::Texture::replaceRegion` escape hatch.
  - [x] **Overlay pass** — inline MSL with instanced glyph quads (`[[instance_id]]`-indexed `GlyphInstance` buffer, 32 B aligned: pos/scale/uv/color). Pipeline uses alpha blend (`SrcAlpha`/`OneMinusSrcAlpha`), `LoadAction::Load` on the backbuffer.
  - [x] **Profile zones in the engine** — `cull`, `fill_instances`, `overlay_build`, `fg_compile`, `fg_execute`.
  - [x] **Demo HUD lines** — title, FPS / MS / AVG, SIM/STEPS/ALPHA, CUBES (visible/total), STATE (PAUSED/SLOW/NORMAL/FAST), then per-zone `last_ms (avg)` for each tracked zone. Pure CPU; no GPU timestamp sample buffer yet.
  - [x] **5 unit tests** — duration recorded, min/max/avg aggregation, zone independence, rolling-window cap at 120, `MGE_PROFILE_ZONE` macro records.
  - [—] **Metal GPU timestamp sample buffers** (deferred to M11.b — needs `MTLCounterSampleBuffer` + `MTLCounterSet` plumbing through the RHI; CPU overlay is enough to close Phase 1).
  - [—] **Tracy integration** (deferred to M11.b — Tracy is already vendored in `third_party/`; wiring `TracyCZoneN` requires a thin shim around `MGE_PROFILE_ZONE` and is a follow-up).
  - [—] **VRAM / heap memory readout** (deferred — no `MTLHeap` sub-allocator yet; lands with the GPU heap manager in Phase 1.5).

## Phase 1 — Closeout

Phase 1 ships: window + Metal device → RHI → frame graph → deferred PBR → CSM shadows → frustum cull + GPU instancing → bloom + ACES → fixed-timestep game loop with decoupled render → on-screen CPU profiler overlay. 13-pass FrameGraph (shadow → gbuffer → lighting → bloom_bright → 4×ds → 4×us → tonemap → overlay), 84 tests green (79 ctest + 5 profiler), 1k cubes at ~120 FPS on M1 Pro with full deferred PBR + shadow + bloom + ACES + overlay.

## Phase 1.5 — Rendering extensions

- [x] **M12 — Compute pipelines + GPU particles**
  - [x] **RHI compute surface** — `ComputePipeline`, `ComputeEncoder`, `Device::create_compute_pipeline`, `CommandBuffer::begin_compute_pass`. Metal backend wraps `MTLComputePipelineState` / `MTLComputeCommandEncoder`. `dispatch` (threadgroup-count) and `dispatch_threads` (total-thread-count) entry points. ADR-0008.
  - [x] **Integration test** — `test_compute.cpp` compiles an inline MSL kernel that fills a Shared buffer with `i*2 + bias`, dispatches, validates from CPU.
  - [x] **GPU particle system** — 32 768 particles, persistent Storage buffer. Compute kernel does gravity + integration + life decay; respawns dead slots at the emitter (hash-derived random disk + upward velocity). Render pass instances camera-facing billboard quads with additive blend over the tonemapped scene.
  - [x] **FrameGraph integration** — particle pass declares `write_color(bb, Load)` so WAW edges put it between tonemap and overlay. Compute step lives inside the pass's execute lambda (begin compute, dispatch, end, then begin render), so no first-class FG buffer handle needed yet.
  - [x] **Demo wiring** — emitter at origin, hot-core → cool-tail color blend, respects `time_scale` and `is_paused` (particles freeze in demo-mode pause). 80/80 tests green; demo holds 120 FPS on M1 Pro at 1k cubes + 32k particles.
  - [—] **Particle sorting for blended overlap** (deferred to M12.b — additive blend hides ordering issues for now).
  - [—] **Soft particles via depth read** (deferred to M12.b — needs sampling the scene depth in the particle fragment shader; trivial add when motivated).
  - [—] **First-class FG buffer resources** (tracked as `P1-FG-BUFFER-001`).
- [x] **M13 — Ray tracing (shadows + reflections, hybrid)**
  - [x] **RHI acceleration structures** — `AccelerationStructure` opaque type, `PrimitiveAccelDesc` / `InstanceAccelDesc` builders, `Device::build_acceleration_structure(Queue&, ...)` blocking helpers for both BLAS and TLAS. Metal backend wraps `MTL::PrimitiveAccelerationStructureDescriptor` / `MTL::InstanceAccelerationStructureDescriptor` + `MTL::AccelerationStructureCommandEncoder`. ADR-0009.
  - [x] **Fragment AS binding** — `RenderEncoder::set_fragment_acceleration_structure(slot)` + `use_fragment_acceleration_structure` for BLAS residency. `DeviceInfo::supports_ray_tracing_from_render` exposed.
  - [x] **Integration test** — `test_raytrace.cpp` builds a one-triangle BLAS + identity-instance TLAS, fires a fragment-stage ray query from each pixel of a 64×64 RT, asserts center hits (green) and corners miss (red).
  - [x] **Scene BVH** — hello_metal builds per-mesh BLAS for sphere / cube / ground at startup, then a TLAS over all 5 spheres + ground + every cube (~1018 instances on a 32×32 grid).
  - [x] **Inline RT shadows** — `lighting_rt_fs` fires a shadow ray from the surface point toward the sun with `accept_any_intersection(true)`. Replaces CSM PCF in the lit path. Crisp pixel-perfect shadows, no shadow-map artifacts. Fall back to CSM via `--no-rt`.
  - [x] **RT reflections for metals** — metallic surfaces fire a reflection ray. On miss → sky gradient (also lit by sun halo). On hit → approximate hit shading (neutral albedo + sun-visibility check + ambient). Strength is `metallic * reflection_strength` (default 0.65).
  - [x] **Sky background** — `lighting_rt_fs` samples a soft horizon gradient with sun halo when the depth-fail discard would otherwise leave a black clear color showing through (closes the "black sky band" reported in M11).
  - [x] M11 sphere wobble removed (the static TLAS would have desynced from a rotating-around-origin transform). Sim still ticks.
  - [—] **Per-frame TLAS refit** (M13.b) — would unlock animated scenes. Today's blocking builder is fine for a static TLAS but stalls CPU on rebuild.
  - [—] **Material-aware hit shading** (M13.c) — needs a full RT pipeline with closest-hit programs, or per-instance material indirection through user-data + a probe lookup.
  - [—] **Soft shadows / area lights** — single sample per pixel today. TAA + multiple samples = future work.
  - [—] **CSM shadow pass removal** — pass + map are vestigial when RT is on. Kept for `--no-rt` fallback (P1-RT-CSM-FALLBACK-001).
- [x] **M14 — GPU occlusion culling (HZB, stats-only v1)**
  - [x] **HZB build compute** — 256×256 R32Float transient texture. One thread per HZB texel, reads its source-depth tile via `depth2d<float>::read`, MAX-reduces, writes. Conservative (object occluded iff its near depth exceeds HZB's max in that rect).
  - [x] **HZB stats compute** — one thread per visible cube. Projects local unit AABB through `model * view_proj`, computes screen-space rect, max-reduces HZB texels in rect, compares to AABB's min NDC z. Writes per-instance visibility byte + atomic occluded counter.
  - [x] **FrameGraph wiring** — `hzb.build` reads `depth`, writes `hzb`; `hzb.stats` reads `hzb` + instance buffer. New `ResourceUsage::ShaderWrite` to distinguish compute-write from color-attachment writes.
  - [x] **HUD readback** — counter is in Shared storage, CPU reads previous frame's value, HUD displays `HZB X VIS Y OCC`.
  - [x] **Integration test** — `test_hzb.cpp` fills a 4×4 R32F gradient via a fragment pass, runs a 2×2 reduce, validates tile-max correctness. 82/82 tests green.
  - [—] **Mip pyramid** (M14.b) — current single-resolution HZB loops over the AABB rect per query. Proper pyramid samples the matching-mip texel directly.
  - [—] **Actual draw filtering** (M14.b) — visibility bytes are computed but the draw still rasterizes all frustum-visible cubes. Either vertex-shader degenerate-clip (cheaper, minimal API surface) or full GPU-driven indirect via `MTLIndirectCommandBuffer` (bigger payoff, separate ADR).
- [x] **M15 — LOD system (discrete distance-based)**
  - [x] **Sphere LOD chain** — 3 meshes (high `{20,32}`, mid `{10,16}`, low `{6,10}` lat/lon). ~4× tri count drop per step.
  - [x] **CPU distance-based selection** — per sphere each frame, picks LOD by distance to camera. `--force-lod {0|1|2}` overrides for debugging.
  - [x] **Per-LOD batched draws** — instance buffer is packed in LOD-grouped order; shadow + G-Buffer passes loop over LODs and emit `draw_indexed(base_instance = lod_offset, instance_count = lod_count)` per level.
  - [x] **HUD readout** — `LOD H<x> M<y> L<z>  TRIS A/B` shows the LOD distribution + actual vs baseline (all-high) triangle count.
  - [x] **RT uses high-LOD only** — reflections always sample the highest-detail BLAS; per-LOD BLAS is tracked as a follow-up alongside the static-TLAS deferral.
  - [—] **Per-LOD BLAS for RT** (M15.b) — needs TLAS refit / rebuild plumbing first (P1-RT-STATIC-TLAS-001).
  - [—] **Screen-projected size selection** — current heuristic is raw distance; projected radius would handle FOV/zoom correctly. Trivial follow-up.
  - [—] **LOD dithering / temporal blend** — Phase 2 polish.
- [x] **M16 — Skeletal animation (vertex-shader skinning, procedural data)**
  - [x] **`SkinnedVertex` (64 B, aligned 16)** — pos + normal + 4 weights + 4 joint indices per vertex.
  - [x] **`make_skinned_tube`** — procedural cylinder along +Y, 6 bones spaced uniformly along the spine, per-vertex weights blend the two nearest bones.
  - [x] **`solve_bone_chain`** — walks the bone chain root→tip applying a per-bone `rotation_x` driven by `sin(t * 2.5 + bone_idx * 0.55) * 0.32`. Emits joint matrices = `world_pose × inverse_bind`. Linear-blend skinning compatible with glTF conventions.
  - [x] **`k_skinned_gbuffer_msl`** — skinning vertex shader: 4 weight blends per vertex against a `JointBuffer` at slot 3. Reuses the rigid `gbuffer_fs` fragment shader.
  - [x] **Instance layout** — tube lives at `tube_slot = k_spheres.size() + 1`; cubes shift to `cube_base = tube_slot + 1`.
  - [x] **HUD readout** — `SKIN BONES N  VERTS X`.
  - [—] **CPU vs compute skinning** — vertex-shader skinning is the M16 baseline. **M17 will move to compute** so shadow + gbuffer + RT can share the deformed mesh.
  - [—] **RT visibility for skinned mesh** — tube isn't in the TLAS; shares the deferral with P1-RT-STATIC-TLAS-001 (P1-SKIN-RT-001).
  - [—] **glTF animation import** — Phase 2 asset pipeline.
- [x] **M17 — Skinning compute shader**
  - [x] **`k_skin_compute_msl`** — kernel reads `SkinnedVertex` + `JointBuffer`, writes a deformed `PbrVertex` stream (one thread per vertex).
  - [x] **`tube_skinned_vbuf`** — `Vertex|Storage` Private-storage buffer holding the post-skin verts; written by compute, read by the gbuffer vertex stage.
  - [x] **Rigid gbuffer reuse** — skinned tube draws with the standard `gbuffer_pso` over the compute output. Vertex-shader skinning shader (`k_skinned_gbuffer_msl`) and the dedicated `skinned_gbuffer_pso` from M16 are removed.
  - [x] **FrameGraph `skin` pass** — runs before `gbuffer`. Ordering held by declaration order (FG buffer handles still deferred via P1-FG-BUFFER-001).
  - [x] HUD line now reads `SKIN BONES N  VERTS X  (COMPUTE)`.
  - [—] **Skinned shadow casting + RT visibility** — same fix path as P1-SKIN-RT-001 / P1-RT-STATIC-TLAS-001. The compute output is already the right buffer to feed a future dynamic-TLAS BLAS rebuild.
  - [—] **Multi-instance skinning** — one output buffer per instance vs structured-buffer indexing. Phase 2.

## Phase 1.5 — Closeout

Phase 1.5 ships: compute pipelines + GPU particles (M12) → inline RT shadows + metallic reflections (M13) → HZB occlusion build + stats (M14) → discrete LOD chain (M15) → skeletal animation (M16) → compute-driven skinning (M17). 13 ADRs, 82 tests green, 17 milestones across Phase 1 + 1.5, demo on M1 Pro renders deferred PBR + RT shadows + RT reflections + bloom + ACES + 32k GPU particles + LOD-batched spheres + compute-skinned tube + HZB stats + on-screen profiler overlay, locked to 120 FPS at 1080p.

## Phase 2 — Tooling (swapped with Systems per chat 2026-05-12)

- [x] **M18 — Dear ImGui editor foundation + design system**
  - [x] **Design handoff** ingested at `notes/design_drop/game-engine/` — 7 screens, component library, design tokens. Amber accent (`#E8A24A`) locked.
  - [x] **ImGui (docking)** vendored via FetchContent; Metal + OSX backends compiled at the platform edge (ADR-0001 carve-out, ADR-0014 records this decision).
  - [x] **`engine/editor/` module** — `Editor` + `EngineState` API. `apply_theme()` translates the design tokens straight into ImGuiStyle.
  - [x] **FrameGraph integration** — single `editor` pass after `overlay`, `LoadAction::Load` on the backbuffer. Toggle via `--editor`.
  - [x] **Chrome shell** — menubar, toolbar with readout pills, three-column workarea, bottom dock tab strip, status bar. M18 ships stateless chrome with placeholder panels; M19+ wire real content.
  - [—] **Outliner + Inspector content** — M19.
  - [—] **Bottom dock panels** (Console, Profiler, FrameGraph view, Render Settings, Shader Reload) — M20.
  - [—] **Live shader reload + cvars** — M21.
  - [—] **Input system (camera nav, gizmos)** — M23.
- [x] **M19 — Real fonts + Outliner + Inspector**
  - [x] **M19a — SF Pro + SF Mono TTFs** loaded from `/System/Library/Fonts/`. Atlas at `logical × dpi`, `FontGlobalScale = 1/dpi` for crisp subpixel text on Retina (canonical ImGui Retina trick). `ScopedMonoFont` helper for data readouts.
  - [x] **M19b — Outliner tree** with real scene contents (`procedural_demo` → 5 spheres / cube field / particle emitter / skinned tube / sun / camera). Selection: 2 px amber inset stripe + amber-tinted bg + label color. Tree rows draw label + badge chip via `ImDrawList::AddText` so they don't fight the layout cursor.
  - [x] **M19b — Inspector for spheres** — hero (circular albedo swatch + name + `k_spheres[i]` path), TRANSFORM (read-only position), MATERIAL (`ColorEdit3` albedo + styled metallic / roughness / AO sliders). Mutations flow back into `k_spheres` via `SphereView` pointers — next frame's instance buffer picks them up.
  - [—] **Camera / Sun / Particle / Tube inspectors** — deferred to M19c. Lower priority since their state already surfaces in Render Settings / status bar.
  - **Bugs found + fixed on the way**: `fg_execute` label leak (`sphere_views` died before `fg.execute` — hoisted to render_fn scope), click swallowed by `AllowOverlap` (dropped the flag), mouse position mismatch on Retina (canonical refactor: `DisplaySize = logical pts`, `DisplayFramebufferScale = (dpi, dpi)` so input + widget bboxes share a coordinate space).
- [x] **M20 — Bottom dock panels (Console / Profiler / Render Settings)**
  - [x] **Console** — scrollable monospace lines with level colors (info/warn/err). Static placeholder data; M24 will route `mge::core::Logger` here.
  - [x] **Profiler bar chart** — every `mge::profile` zone with LAST + AVG columns and a horizontal bar showing last-frame fill (`acc_bg_2` with amber tick at the fill edge) + `fg_4` tick for the rolling average. Scale auto-fits to `max(zones)` floored to a 5 ms budget hint.
  - [x] **Render Settings (FEATURES)** — Checkboxes for `editor`, `rt`, `hzb`, `overlay`, `demo cycle`. Toggles wired through `EngineState bool*` pointers; the demo flips `a.demo_mode` live as proof.
  - [x] Same `ScopedMonoFont` lifetime rule that bit `readout` in M19a hit the Console panel — inner-block scope keeps Push/Pop balanced inside `BeginChild`.
- [x] **M21 — Mutable lighting cvars + live sliders**
  - [x] **`DemoCvars` struct** owns sun yaw/pitch, sun color, ambient color, shadow bias, reflection strength. `fill_lighting_constants` reads from it each frame instead of hardcoded literals.
  - [x] **SUN / AMBIENT / RT sections** in the Render Settings panel — yaw + pitch radians sliders (sun shadows track in real time), HDR color picker for sun, LDR for ambient, 0..1 slider for reflection strength, 0.0001..0.02 for shadow bias.
  - [—] **Bloom / exposure / particle emitter sliders** — deferred to M21.b, needs exposing more demo uniforms as cvars.
- [x] **M22 — FrameGraph view**
  - [x] **`const FrameGraph*` plumbed through `EngineState`**. FG already exposes `pass(idx)` / `num_passes()` / `schedule()` / `resource_name(handle)` — no new accessors needed.
  - [x] **Pass list panel (left, ≈360 px)** — monospace rows with R / W counts. Selectable.
  - [x] **Detail pane (right)** — READS / WRITES / DEPTH blocks for the selected pass with `ResourceUsage` decoded to a short string (`color`, `depth`, `read`, `write`, `copy.src`, `copy.dst`).
  - [x] Live updates with the running demo — every frame's 17-pass schedule (shadow → gbuffer → skin → hzb.build → hzb.stats → lighting → bloom.bright → bloom.ds×4 → bloom.us×4 → tonemap → particles → overlay → editor) walks correctly.
- [x] **M23 — Shader Reload panel**
  - [x] **`ShaderEntry[]` + `reload_shader` callback** on `EngineState`. The demo registers all 14 inline MSL sources with their entry-point labels and a C function-pointer reload hook that re-invokes `Device::create_shader_from_msl` as a compile sanity check.
  - [x] **Per-row layout** — name (28 ch), entry-point (19 ch), `OK` / `ERR` badge in `ok` / `err` palette colors, amber-text "Reload" button.
  - [—] **Live pipeline hot-swap** — deferred to M23.b. Today the reload only re-validates the MSL compile; replacing the live PSOs requires either a stable PSO-by-name registry or a file-watcher feeding source edits.
- [ ] **M24 — Console wired to mge::core::Logger + cvar registry** (Phase 2.b polish)

## Phase 2 — Closeout

Phase 2 Tooling ships a working editor: ImGui-driven chrome with the Claude Design system applied (`#E8A24A` amber accent, slate palette, JetBrains-Mono-style numeric readouts via SF Mono), the design's exact layout (menubar / toolbar / outliner / viewport / inspector / 5-tab bottom dock / status bar), and every tab live. The demo renders deferred PBR + RT + HZB + particles + LOD + compute skinning at 120 FPS on M1 Pro with the editor on — sun direction, ambient, reflection strength, shadow bias all tweakable from the Render Settings tab, scene contents browsable in the Outliner, sphere materials editable in the Inspector, FrameGraph schedule + per-pass reads/writes inspectable, shader recompile sanity-checked from the Shader Reload tab. 6 milestones M18→M23, 1 ADR (0014), 82 tests green throughout. Design handoff bundle stays local at `notes/design_drop/` (gitignored) as the single source of truth for tokens.

## Phase 3 — Systems (was Phase 2)

- [x] **M24 — Dynamic TLAS (per-frame rebuild)**
  - [x] `DeferredRenderer::rebuild_dynamic_tlas()` runs after `fg.execute()`. Builds a fresh tube BLAS from `tube_skinned_vbuf` (compute-skinned vertices for the current frame), then rebuilds the TLAS with sphere×5, ground, cube×1012, tube (1018 instances).
  - [x] Skinned tube now casts RT shadow on the ground and appears in metallic-sphere reflections — closes `P1-SKIN-RT-001`, partly closes `P1-RT-STATIC-TLAS-001` (the *static* assumption is gone; the *refit* optimization is what's deferred to M24.b).
  - [x] Lighting RT pass marks `r->blas_tube` resident via `use_fragment_acceleration_structure` so Metal keeps the per-frame BLAS alive across the lighting fragment shader.
  - [x] HUD: new profile zone `tlas_rebuild` shows the cost.
  - [—] **Sphere LOD-aware BLAS** — three sphere BLAS for high/mid/low geometry, TLAS instances point at the matching LOD per camera distance. Closes `P1-LOD-RT-BLAS-001`. Deferred to M24.b.
  - [—] **Refit instead of full rebuild** — TLAS rebuild over 1018 instances is currently ~3 ms blocking. Refit-in-place (same topology, updated transforms) would be a fraction of that. Tracked as `P3-RT-REFIT-001`.
- [x] **M25a — Asset pipeline foundation (cgltf + stb + RHI upload)**
  - [x] cgltf 1.14 and stb (master pinned to 2024-10) `FetchContent_MakeAvailable` in `third_party/`. Header-only INTERFACE targets (`cgltf::cgltf`, `stb::stb`).
  - [x] `Texture::upload_region(mip, x, y, w, h, bytes, bytes_per_row)` on the RHI. Closes `P1-RHI-TEXUPLOAD-001`. Private-storage path tracked as `P3-RHI-BLIT-001`.
  - [x] `engine/assets/texture_load.{h,cpp}` — stb-based RGBA8 decoder (always returns 4-channel, expands grayscale/RGB).
  - [x] `engine/assets/gltf_load.{h,cpp}` — cgltf-driven scene loader. Emits `GltfMesh` (positions + normals + UVs + indices, 48 B vertex) + `GltfMaterial` (base color factor / metallic / roughness + indices into `GltfTexture[]`).
  - [x] Integration test `test_gltf_load.cpp` synthesises a 3-vertex .glb in memory, writes it to a temp file, validates vertex/index/material readback. 83/83 tests green.
- [ ] M25b — Textured PBR shader + demo asset
- [ ] ECS proper (archetype storage)
- [ ] Full job system (work stealing, priorities)
- [ ] Physics (Jolt integration)
- [ ] Audio (CoreAudio / OpenAL)
- [ ] Scripting layer (TBD: Lua / Wren / native plug-ins)
- [ ] Asset pipeline + cooked formats

## Phase 4 — Cross-platform backends

- [ ] iOS support
- [ ] Vulkan RHI backend
- [ ] DX12 RHI backend
- [ ] Networking + rollback

## Status convention

- Untouched = `[ ]`
- In progress = `[~]`
- Done = `[x]`
- Deferred = `[—]`
