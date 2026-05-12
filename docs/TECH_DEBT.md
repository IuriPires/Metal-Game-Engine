# Tech Debt

Track of conscious shortcuts. Each entry has: what, why, cost, fix path.

## Format

```
### [PHASE-COMPONENT-N] Short title
- **What**: ...
- **Why now**: ...
- **Cost if left**: ...
- **Fix path**: ...
- **Owner**: ...
- **Created**: YYYY-MM-DD
```

---

## Open debt

### [P1-CORE-001] Logger and assert allocate on cold paths
- **What**: `Logger::format` and assertion message formatting use `std::format`/`fmt`, which may allocate.
- **Why now**: Phase 1 doesn't have a stable string-pool allocator. Hot paths bypass logging anyway.
- **Cost if left**: Memory noise in profiling tools; not measurable in steady-state.
- **Fix path**: Add a frame-local logger formatter using `Arena` in Phase 2.
- **Owner**: core
- **Created**: 2026-05-12

### [P1-JOBS-001] Threadpool uses `std::function`
- **What**: Job callable storage is `std::function`, which can allocate.
- **Why now**: Phase 1 job system is intentionally minimal.
- **Cost if left**: Heap alloc per submitted job. Negligible for ≤1k jobs/frame; visible if we scale to GPU-driven dispatch.
- **Fix path**: Replace with SBO functor wrapper (24 bytes inline + heap fallback) in Phase 2.
- **Owner**: jobs
- **Created**: 2026-05-12

### [P1-DET-001] Determinism is same-binary-same-machine only
- **What**: Cross-compiler / cross-CPU determinism not guaranteed.
- **Why now**: Float math reproducibility across SSE/NEON/x87 evaluation paths requires non-trivial work.
- **Cost if left**: Network rollback netcode cannot be built directly on Phase 1 sim.
- **Fix path**: Strict-fp, deterministic transcendental table, audit reductions. Phase 2.
- **Owner**: sim
- **Created**: 2026-05-12

### [P1-RHI-001] RHI starts as a thin Metal mirror, not a clean Vulkan/D3D12 abstraction
- **What**: Some early RHI types may leak Metal terminology (e.g., heap semantics, argument buffers).
- **Why now**: Premature abstraction kills velocity. We have one backend.
- **Cost if left**: Adding Vulkan/DX12 in Phase 4 will require an RHI refactor.
- **Fix path**: Audit RHI surface at the start of Phase 4 with a real second backend in flight. Will produce ADR.
- **Owner**: renderer
- **Created**: 2026-05-12

### [P1-FG-001] Frame graph v1 has no async compute
- **What**: Single graphics queue only. Async-eligible passes are tagged but not scheduled in parallel.
- **Why now**: Async compute scheduling correctness is hard; not blocking 120 FPS Phase 1 budget on M-series.
- **Cost if left**: Leaving 10–25% of GPU on the table for compute-heavy frames.
- **Fix path**: Add second queue + barrier insertion in Phase 1.5 / Phase 2.
- **Owner**: renderer
- **Created**: 2026-05-12

### [P1-ASSETS-001] No cooked asset pipeline
- **What**: Phase 1 loads glTF and PNG at runtime via cgltf + stb_image.
- **Why now**: Asset cooker is a Phase 2 project.
- **Cost if left**: Slow startup with big scenes, no LZ4/Zstd compression, no platform-specific texture compression (ASTC).
- **Fix path**: Asset cooker tool + cooked format in Phase 2. ASTC compression in Phase 1.5.
- **Owner**: assets
- **Created**: 2026-05-12

### [P1-OBJC-001] Obj-C++ shim at platform edge — RESOLVED (M1, 2026-05-12)
- **Decision**: Two `.mm` files (`app.mm`, `window.mm`) under `engine/platform/macos/src/` are the only Obj-C++ in the engine. They expose pure-C++ APIs (`mge::platform::App`, `mge::platform::Window`). ADR-0001 updated with the integration notes. Tech-debt status: closed, not "open".

---

## Closed debt

- **P1-OBJC-001** — Obj-C++ shim at platform edge: resolved in M1.
- **P1-WINDOW-001** — Window resize handling: resolved in M4.
- **P1-RHI-DRAW-001** — RenderEncoder topology hardcode: resolved in M4.

## New debt opened in M1

### [P1-METAL-001] metal-cpp framework-search workaround
- **What**: We pass metal-cpp's include path without the `metal-cpp/` directory prefix so that `<Foundation/Foundation.hpp>` resolves through Apple Clang's framework lookup miss-then-fall-through. Fragile if Apple ever changes Clang's lookup order.
- **Why now**: It works today on Apple Clang 17 and unblocks M1.
- **Cost if left**: A future toolchain bump could break the include path; we'd detect it immediately on CI.
- **Fix path**: If it breaks, pre-create a symlink directory inside the build tree (`${binary_dir}/metal-cpp-wrapper/metal-cpp -> ${metal_cpp_SOURCE_DIR}`) and add the wrapper to the include path. Documented in ADR-0001.
- **Owner**: renderer/metal
- **Created**: 2026-05-12

### [P1-WINDOW-001] Window resize handling — RESOLVED (M4, 2026-05-12)
- **Decision**: `MGEWindowDelegate` now observes `windowDidResize:` / `windowDidChangeBackingProperties:`. `Window::consume_resize_event()` returns a one-shot bool and resyncs the CAMetalLayer's `drawableSize` to the view's backing size. hello_metal calls `Swapchain::resize` and reallocates the depth texture on each event; the camera updates its aspect.

## New debt opened in M3

### [P1-RHI-MOCK-001] No mock RHI backend yet
- **What**: Frame-graph / pass-level unit tests cannot run without a real Metal device. Currently all GPU coverage is in the integration suite.
- **Why now**: M3 budget didn't include a full mock; M5's compile() is testable directly without execute(), so the urgency dropped.
- **Cost if left**: CI on non-Metal runners (Linux) cannot run RHI tests. M9+ perf-gate work may want mockable backends.
- **Fix path**: Build `mge_rhi_mock` when iOS/Vulkan ports start, OR before Phase 1.5 if cross-platform CI is needed.
- **Owner**: renderer/rhi
- **Created**: 2026-05-12, downgraded 2026-05-12

## New debt opened in M6

### [P1-PBR-IBL-001] No IBL yet
- **What**: M6 ships direct lighting only (one directional sun + ambient term). No image-based lighting → indirect diffuse and specular are crude.
- **Why now**: IBL needs (a) HDR equirectangular loader, (b) compute pipeline to bake irradiance + prefiltered specular cube, (c) BRDF LUT generation. That's a full milestone on its own.
- **Cost if left**: Scenes look "ambient-flat" — no reflections, no environment colour bleed.
- **Fix path**: M6.1 (post-M11): add compute pipeline support to RHI; HDR equirectangular loader; offline (or first-frame) IBL baking; split-sum sampling in the lighting pass.
- **Owner**: renderer
- **Created**: 2026-05-12

### [P1-PBR-MULTISCATTER-001] No multiscatter compensation
- **What**: Lighting uses single-scattering GGX. Energy lost to internal microfacet scattering is not redistributed; high-roughness metals look dimmer than they should.
- **Why now**: Most visible alongside IBL anyway. Direct-lighting-only is forgiving.
- **Cost if left**: ~10-15% energy loss at roughness ≈ 0.8 with metals.
- **Fix path**: Fdez-Agüera 2019 (analytic, no LUT needed). ~5 lines in the lighting fragment shader.
- **Owner**: renderer
- **Created**: 2026-05-12

### [P1-PBR-LIGHTS-001] Single directional light, no clustering
- **What**: Lighting pass hard-codes one sun. No point/spot lights. No clustered light culling.
- **Why now**: M6 budget priorisation. Forward+ transparent pass + clustered cull comes when we need ≥2 lights.
- **Cost if left**: Cannot do interior scenes with multiple punctual lights.
- **Fix path**: Add `LightArray` uniform + cluster pass (compute) writing per-cluster light index list. Phase 1.5 / Phase 2.
- **Owner**: renderer
- **Created**: 2026-05-12

## New debt opened in M5

### [P1-FG-BARRIERS-001] No automatic barriers / transitions yet
- **What**: `FrameGraph` v1 relies on Metal's implicit dependency tracking between render encoders / blit encoders. We don't emit explicit `MTLFence`/event waits; we don't know about ResourceUsage transitions.
- **Why now**: Metal does the right thing in steady state on a single graphics queue. Explicit barriers matter at Phase 1.5 / Phase 4 (Vulkan).
- **Cost if left**: Cannot do async compute. Cannot port to Vulkan/DX12 without finishing this.
- **Fix path**: At Phase 1.5 add a barrier list to the compile output keyed on usage transitions; emit `MTLEvent::signal/wait` between async-eligible passes.
- **Owner**: renderer/frame_graph
- **Created**: 2026-05-12

### [P1-FG-REBUILD-001] Frame graph rebuilt per frame
- **What**: hello_metal calls `fg.reset()` + redeclares passes every frame. compile() runs every frame.
- **Why now**: Simplest correct model. Resize-safe. Cost is negligible at v1 scale (one pass).
- **Cost if left**: When pass count grows (M6+), per-frame compile() may dominate CPU. Frame-graph traversal is O(passes + resources).
- **Fix path**: Pass-hash the declaration; skip recompile when hash unchanged. Or keep a "static" graph + per-frame "dynamic resource updates" for imported textures.
- **Owner**: renderer/frame_graph
- **Created**: 2026-05-12

### [P1-RHI-SHADERS-001] Shaders are inline string literals, runtime-compiled
- **What**: MSL source for every pipeline is a `constexpr const char*` inside the consumer .cpp. Runtime compile via `MTL::Device::newLibrary` on every startup.
- **Why now**: Shader pipeline tooling (offline metallib, hot reload, reflection) is M5/M6 territory.
- **Cost if left**: Slow startup as scenes grow, no compile-time validation in CI.
- **Fix path**: Offline shader compiler tool under `tools/shader_compiler/` produces a `.metallib`; ship with the binary. Hot reload via file watcher in Debug. Pencil for M5.
- **Owner**: renderer/metal
- **Created**: 2026-05-12

### [P1-RHI-DRAW-001] RenderEncoder hard-codes TriangleList — RESOLVED (M4, 2026-05-12)
- **Decision**: `RenderEncoder` caches `topology_` during `set_pipeline()` and forwards it to both `drawPrimitives` and `drawIndexedPrimitives`. Strip / line / point pipelines now route correctly.

## New debt opened in M4

### [P1-CGLTF-001] Procedural primitives only — no asset loader yet
- **What**: M4 ships `make_cube` but no glTF / glb loader. The cgltf dep is still declared-not-active in `third_party/CMakeLists.txt`.
- **Why now**: Procedural primitives cover the M4 demo and integration test without paying for asset I/O.
- **Cost if left**: M5+ cannot demo realistic scenes (Sponza-class).
- **Fix path**: Enable cgltf via FetchContent; write a minimal loader that produces an `assets::Mesh` from a single-primitive glTF file. Add a stb_image-backed texture loader at M6.
- **Owner**: assets
- **Created**: 2026-05-12

### [P1-INPUT-001] No input system - camera is fixed
- **What**: `App::poll_events()` drains NSApp's queue but doesn't expose keyboard/mouse state to higher layers. The M4 demo orbits the cube via wall-clock time.
- **Why now**: An FPS controller wants keyboard polling + mouse delta; that's a non-trivial input subsystem out of scope for M4.
- **Cost if left**: No interactive camera, no editor-style nav.
- **Fix path**: Add `mge::platform::Input` with keystate + mouse delta, drained inside `poll_events`. Pencil for M5 alongside the frame graph.
- **Owner**: platform
- **Created**: 2026-05-12
