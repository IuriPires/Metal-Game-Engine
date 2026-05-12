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
- [ ] **M2 — Core systems**
  - [ ] Logger, Assert, Result<T,E>, Time, Config
  - [ ] Custom SIMD math (vec2/3/4, mat4, quat)
  - [ ] Arena, Pool allocators
  - [ ] Unit tests: math invariants, allocator correctness
- [ ] **M3 — RHI v1 + first triangle**
  - [ ] RHI types: Device, Queue, CmdBuffer, Pipeline, Buffer, Texture, Sampler, Fence
  - [ ] Metal backend implementation
  - [ ] Triangle drawn via RHI
  - [ ] Mock backend for unit tests
  - [ ] Integration: triangle golden image
- [ ] **M4 — Mesh + camera + basic forward**
  - [ ] cgltf glTF loader
  - [ ] MeshBuffer (vertex, index, attribute layout)
  - [ ] Perspective camera + FPS controller
  - [ ] Basic Lambert forward shader
  - [ ] Golden image of a cube/sphere
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
