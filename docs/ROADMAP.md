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
- [ ] **M5 — Frame graph v1**
  - [ ] Pass declaration API
  - [ ] Virtual resources + lifetime analysis
  - [ ] Automatic transitions and barriers
  - [ ] Transient pool aliasing
  - [ ] Graphviz dump
  - [ ] Unit tests: topology, aliasing correctness
- [ ] **M6 — Deferred PBR + IBL**
  - [ ] G-Buffer pass
  - [ ] Clustered light culling (compute)
  - [ ] PBR BRDF (GGX-Smith + multiscatter compensation)
  - [ ] Prefiltered IBL pipeline (offline tool)
  - [ ] BRDF LUT generation
  - [ ] Golden: furnace test, reference scene
- [ ] **M7 — CSM shadows**
  - [ ] 4-cascade CSM
  - [ ] PCF 3×3 filtering
  - [ ] Stable texel snap
  - [ ] Golden: shadow scene
- [ ] **M8 — Culling + instancing**
  - [ ] CPU frustum cull (NEON SoA)
  - [ ] GPU instancing via `MTLIndirectCommandBuffer`
  - [ ] Perf: 50k cubes ≥ 120 FPS
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
