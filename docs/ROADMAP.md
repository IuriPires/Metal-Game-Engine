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
- [ ] **M9 — Post FX chain**
  - [ ] HDR pipeline
  - [ ] Auto-exposure (compute histogram)
  - [ ] Bloom (down/up pyramid)
  - [ ] Motion vectors integrated in GBuffer
  - [ ] TAA with neighborhood clamp
  - [ ] ACES tonemap
- [ ] **M10 — Game loop**
  - [ ] Fixed timestep accumulator
  - [ ] Decoupled render with interpolation
  - [ ] Pause + time scale
  - [ ] Deterministic mode + replay test
  - [ ] Frame pacing under target fps
  - [ ] Triple-buffered frame data
- [ ] **M11 — Profiling overlay**
  - [ ] On-screen CPU/GPU/mem/VRAM widget
  - [ ] Tracy zones across the engine
  - [ ] Metal GPU timestamp sampling

## Phase 1.5 — Rendering extensions

- [ ] Ray tracing (shadows + reflections, hybrid)
- [ ] GPU particles (compute-driven)
- [ ] GPU occlusion culling (depth pyramid + HZB)
- [ ] LOD system
- [ ] Skeletal animation (mesh skinning + animation graph)
- [ ] Skinning compute shader

## Phase 2 — Systems

- [ ] ECS proper (archetype storage)
- [ ] Full job system (work stealing, priorities)
- [ ] Physics (Jolt integration)
- [ ] Audio (CoreAudio / OpenAL)
- [ ] Scripting layer (TBD: Lua / Wren / native plug-ins)
- [ ] Asset pipeline + cooked formats

## Phase 3 — Tooling

- [ ] Editor (Dear ImGui)
- [ ] Frame capture viewer
- [ ] Live shader reload UX
- [ ] Live tweak / cvar system

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
