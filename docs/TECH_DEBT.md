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

## New debt opened in M10

### [P1-SIM-THREAD-001] Sim runs on the render thread
- **What**: `GameLoop::tick(sim, render)` calls sim and render back-to-back on the same thread. No dedicated simulation thread, no triple-buffered snapshot ring.
- **Why now**: Single-threaded sim is correct + deterministic at Phase 1's scope (one sphere wobble). Threading is an architectural step that warrants its own milestone.
- **Cost if left**: Cannot land networking rollback or large CPU-bound sims without the threading model. Render currently blocks if sim is slow.
- **Fix path**: Phase 1.5+. Add a `SimSnapshotRing<T>` template with 3 slots, run sim on a dedicated thread feeding the ring, render reads the two most-recent and interpolates.
- **Owner**: gameplay / core
- **Created**: 2026-05-12

### [P1-LOOP-PACING-001] Frame pacing competes with vsync
- **What**: `pace_to_target` sleeps + spins to hit `target_fps`. With CAMetalLayer vsync on, the present already enforces 120 Hz, so our pacer almost always finds itself behind ("snap forward and return"). It's harmless but redundant.
- **Why now**: Same code will be load-bearing when running with vsync off, or against displays with refresh rates other than 120.
- **Cost if left**: None at current settings; a small amount of dead code per frame.
- **Fix path**: Detect vsync mode from the swapchain and gate pacing. Or expose `--vsync off` and verify pacing controller hits target then.
- **Owner**: core / platform
- **Created**: 2026-05-12

## New debt opened in M9

### [P1-POSTFX-AUTOEXP-001] No auto-exposure
- **What**: Sun intensity, ambient, and tonemap exposure are all hard-coded. Auto-exposure (luminance histogram + adaptation) is deferred.
- **Why now**: Requires compute pipeline support in the RHI (or a mip-reduce fragment chain). M9 v1 scope was bloom + ACES.
- **Cost if left**: Scenes can't gracefully transition between bright outdoor and dim indoor exposures.
- **Fix path**: M9.b — add `ComputePipeline` to RHI; compute pass builds a 256-bin histogram of HDR luminance; second compute pass reduces to a single exposure value with EMA adaptation; tonemap multiplies HDR by exposure before ACES.
- **Owner**: renderer/postfx
- **Created**: 2026-05-12

### [P1-POSTFX-TAA-001] No motion vectors, no TAA
- **What**: G-Buffer has no motion vectors. No history texture, no temporal anti-aliasing. Aliasing visible on shadow edges and cube silhouettes.
- **Why now**: TAA is a milestone-sized feature: previous-frame matrices, jittered projection, history reprojection, neighborhood clamp + YCoCg variance trick. Out of M9 v1 scope.
- **Cost if left**: Spatial-only aliasing (no MSAA on deferred either). Bloom helps mask it but doesn't solve it.
- **Fix path**: M9.b. Add a motion-vector channel to the GBuffer (or a dedicated RGBA16F MV target). Cache previous view-projection on the camera. Add a history HDR texture (imported resource per frame). TAA fragment in the lighting chain post-bloom.
- **Owner**: renderer/postfx
- **Created**: 2026-05-12

## New debt opened in M8

### [P1-CULL-NEON-001] Scalar cull loop in demo, NEON path unused
- **What**: The demo loops over cubes one-at-a-time with the scalar `aabb_visible`. The NEON 4-wide path `aabb_visible_x4_neon` exists and is unit-tested but not wired into the demo's hot loop.
- **Why now**: Scalar cull is already fast enough — 160k AABB tests at ~8 µs total via the compiler's auto-vectorizer. No measurable benefit yet.
- **Cost if left**: When N grows past ~500k or culls happen on multiple frustums (cascades, point lights), the explicit NEON path matters.
- **Fix path**: Hot loop using SoA AABB storage + `aabb_visible_x4_neon` in batches. ~30 lines.
- **Owner**: renderer/culling
- **Created**: 2026-05-12

### [P1-INSTANCE-GPU-CULL-001] CPU-only cull, no GPU-driven path
- **What**: Cull runs on the CPU each frame. For very large scenes (hundreds of thousands), GPU-driven cull + indirect draws is cheaper.
- **Why now**: CPU cull handles 160k+ at <1ms; not the bottleneck.
- **Fix path**: M8.b / Phase 1.5: compute shader cull + `MTLIndirectCommandBuffer` issuing draw_indexed_indirect from a visible-instance buffer the compute pass produces.
- **Owner**: renderer/culling
- **Created**: 2026-05-12

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

## New debt opened in M7

### [P1-SHADOW-CASCADES-001] Single shadow cascade only
- **What**: M7 ships one big 2048×2048 shadow map covering the whole demo scene. No frustum splitting, no per-cascade resolution scaling. Quality at far depths is poor for scenes bigger than the current demo.
- **Why now**: Single-cascade gets the architecture in place (depth-only pipeline, shadow sampling, PCF). Multi-cascade is a follow-on with no fresh architectural risk.
- **Cost if left**: Visible shadow blockiness at far distances; cannot scale to Sponza-class scenes.
- **Fix path**: M7.b — split view frustum into 4 depth slices, compute per-cascade ortho matrices covering each slice's world AABB in light space, snap to texel grid for stability, multi-sample in the lighting fs.
- **Owner**: renderer/shadows
- **Created**: 2026-05-12

### [P1-SHADOW-BIAS-001] Fixed depth bias
- **What**: Shadow bias is a hard-coded 0.0015 constant. No slope-scale bias, no normal offset.
- **Why now**: Works for the demo scene's lighting angle.
- **Cost if left**: Shadow acne or peter-panning visible at grazing angles or with shallow lights.
- **Fix path**: Add slope-scale bias (function of N · L) and/or normal offset. ~10 lines in the shader.
- **Owner**: renderer/shadows
- **Created**: 2026-05-12

### [P1-FG-STORE-001] write_depth defaulted to DontCare — RESOLVED (M6, 2026-05-12)
- **What was wrong**: `PassBuilder::write_depth` set `store_action = DontCare`. M6's deferred lighting pass samples the gbuffer's depth as a shader resource. With DontCare, the depth contents weren't preserved past the gbuffer pass → garbage values → broken world position reconstruction → V/H/NoV/VoH wrong → near-black PBR output.
- **Fix**: default to `StoreAction::Store` in `PassBuilder::write_depth`.
- **Follow-up**: Phase 1.5 should fold this into the FG's lifetime analysis (auto-`DontCare` when no later pass reads the depth) to save Apple TBDR bandwidth.

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

### [P1-PROFILE-GPU-001] No GPU timestamp sampling
- **What**: M11 ships a CPU profiler only. The HUD reports `cull` / `fill_instances` / `fg_compile` / `fg_execute` / `overlay_build` zone times but no per-pass GPU duration.
- **Why now**: `MTLCounterSampleBuffer` + `MTLCounterSet` requires RHI plumbing (counter set discovery, sample buffer creation, per-pass sample boundaries) and a resolve step after each command buffer's `addCompletedHandler`. Worth its own milestone, not a tail of M11.
- **Cost if left**: We can't attribute frame-time spikes to specific GPU passes from the overlay — we still have Xcode GPU Capture if we need it.
- **Fix path**: M11.b. Add `mge::rhi::CounterSampleBuffer`; add `RenderEncoder::sample_counters_at_pass_boundary()`; resolve and feed back into `mge::profile` as `gpu:<pass>` zones.
- **Owner**: renderer/metal + profile
- **Created**: 2026-05-12

### [P1-PROFILE-TRACY-001] Tracy not wired
- **What**: Tracy is vendored in `third_party/` from M0 but `MGE_PROFILE_ZONE` does not forward to `TracyCZoneN` / `TracyCZoneEnd`.
- **Why now**: Phase 1 has < 20 zones and the on-screen overlay is sufficient to validate per-frame behavior. Tracy's value scales with zone count.
- **Cost if left**: No timeline view of zones, no cross-frame flame graphs, no remote inspection. Acceptable for Phase 1.
- **Fix path**: Add a thin macro shim around Tracy's C API behind an `MGE_TRACY` CMake option. Compile-time toggle so Release default still ships zero overhead if unused.
- **Owner**: profile
- **Created**: 2026-05-12

### [P1-PROFILE-FONT-001] Hand-coded bitmap font embedded in the demo
- **What**: `examples/hello_metal/font8x8.h` is a 64-glyph hand-coded 8×8 bitmap font living in the demo, not the engine. ASCII subset only; no kerning, no Unicode, no fallback glyph.
- **Why now**: M11 needs glyphs to render anything; an MSDF / stb_truetype pipeline is overkill for a debug overlay.
- **Cost if left**: Can't render unsupported characters (`snprintf` callers must stick to the subset). Editor / non-Latin text is out of reach.
- **Fix path**: Move to `engine/text/` when the editor lands; add stb_truetype + a SDF atlas baker. Keep the 8×8 font as a fallback debug overlay.
- **Owner**: text (future)
- **Created**: 2026-05-12

### [P1-RHI-TEXUPLOAD-001] Font atlas upload bypasses RHI via metal-cpp escape hatch
- **What**: `examples/hello_metal/main.cpp` uses `MTL::Texture::replaceRegion` directly to upload the 8-bit font atlas. The RHI `Texture` type has no `upload_region` / `replace_region` method yet.
- **Why now**: M11 is the first time we needed CPU→GPU texture data after creation. Adding a proper RHI texture upload path (with staging buffer + blit encoder for private-storage textures) is a real interface change and would have pulled scope on M11.
- **Cost if left**: One Metal-specific call leaks into the demo. The RHI abstraction has a hole for any future texture upload (e.g. glTF base color maps at M6.1, environment maps for IBL).
- **Fix path**: Add `RhiTexture::upload_region(level, x, y, w, h, bytes, bytes_per_row)`; in the Metal backend, blit-encode shared→private when the texture is in `Storage::Private`. Lands with the glTF + texture loader.
- **Owner**: rhi
- **Created**: 2026-05-12
