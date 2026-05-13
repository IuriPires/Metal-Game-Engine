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

### [P1-FG-BUFFER-001] FrameGraph has no first-class buffer resources
- **What**: FrameGraph tracks only Texture handles. Compute passes that produce buffer data (M12 particle simulation) and the render pass that consumes them have no declared dependency — ordering today is established only via the backbuffer write they share.
- **Why now**: For M12 the producer and consumer are the same FG pass (compute runs inside the particle render pass's execute lambda), so a buffer handle would add API surface without buying anything. Will start mattering at M14 (HZB), where one compute pass writes the depth pyramid that a later compute/render pass reads.
- **Cost if left**: Compute → render data dependencies have to be hand-ordered by pass declaration order. Easy to break silently when the graph grows.
- **Fix path**: Add `BufferHandle` to `frame_graph::ResourceTable`, mirror the read/write APIs from textures, hook into WAW + producer→consumer edges. Touch the topological sort; tests already cover the patterns we'd need.
- **Owner**: frame_graph
- **Created**: 2026-05-12

### [P1-PARTICLES-SORT-001] No sort for transparent particles
- **What**: Particle render uses additive blend (One/One) — order-independent for emission, fine for the current fire-glow style. Any future translucent particle (smoke, soft-edged sprite over an opaque background) will exhibit incorrect blending.
- **Why now**: M12 v1 ships a fire-glow look on additive. Sorting 32k+ particles per frame is its own milestone.
- **Cost if left**: Cannot ship sprite-based smoke / soft alpha particles correctly.
- **Fix path**: GPU radix sort by view-space depth, or per-bucket binning (Halton-style) — lands when a non-additive particle style is needed.
- **Owner**: renderer/particles
- **Created**: 2026-05-12

### [P3-RHI-BLIT-001] No blit encoder for Private-storage texture upload
- **What**: `Texture::upload_region` only works on Shared/Managed textures (calls `replaceRegion`). Private-storage textures, which are faster for read-heavy assets, can't be populated from the CPU through the public API.
- **Why now**: M25a only needs Shared uploads (glTF maps are small enough that the Shared cost is fine).
- **Cost if left**: At scale (4K base color maps, IBL env maps) we want Private storage for sustained read perf. Today that path is blocked.
- **Fix path**: Add `Device::create_blit_encoder()` on `CommandBuffer` (`MTL::BlitCommandEncoder`). Implement an upload helper that:
  - Allocates a Shared staging buffer
  - `memcpy`s CPU bytes into it
  - Encodes `copyFromBuffer:sourceOffset:...:toTexture:destinationSlice:...` on the blit encoder
- **Owner**: rhi
- **Created**: 2026-05-12

### [P3-INPUT-EDITOR-OFF-001] No camera input without --editor
- **What**: M26a routes camera input through `Editor::input_state()`, which is populated from `ImGuiIO`. When `--editor` is not passed, ImGui isn't bootstrapped and the engine has no event source — the camera stays at its initial `look_at` pose for the whole run.
- **Why now**: ADR-0015 records the decision to use ImGuiIO as the primary input source for Phase 2. Direct NSEvent capture in the platform layer is a separate slice (M26b) that wants to land alongside the multi-viewport / ortho-camera work.
- **Cost if left**: Demo runs (e.g. CI smoke, `--frames N` headless-ish) can't move the camera. Acceptable while the engine is authored from inside the editor.
- **Fix path**: M26b. Add an event queue in `engine/platform/macos/Window` populated by an NSEvent monitor on the content view; expose `Window::poll_input(InputState&)`. When `--editor` is on, fall back to the editor's snapshot; when off, use the platform queue. Either source feeds the same `InputState`.
- **Owner**: platform + editor
- **Created**: 2026-05-13

### [P3-CAM-ORTHO-001] Single perspective camera; no ortho / multi-view yet
- **What**: M26a ships one camera, one controller pair (Fly + Orbit). No orthographic projection, no top/front/side views, no viewport selector in the toolbar.
- **Why now**: M26a scoped to "make the existing camera interactive". Ortho cameras want their own projection helper (`mge::math::orthographic_rh_zo` already exists for shadows — reusable) plus UI for switching, which expanded the slice past a single milestone.
- **Cost if left**: Editor users can't quickly check alignment / silhouette from canonical axes. Tooling for level layout is limited.
- **Fix path**: M26b. Add a `CameraComponent`-ish list of `{name, mode, projection, controller}` records in the demo. Toolbar dropdown + `1/3/7/0` hotkeys swap the active entry. `Camera::set_orthographic(width, height, znear, zfar)` mirrors `set_perspective`. Selector wires through `EngineState`.
- **Owner**: editor + scene
- **Created**: 2026-05-13

### [P2-EDITOR-IMGUI-OSX-SHUTDOWN-001] ImGui OSX backend asserts on process exit
- **What**: `Assertion failed: (Window == nullptr), function ~ImGui_ImplOSX_ViewportData, file imgui_impl_osx.mm, line 891.` fires after the demo prints its exit summary. The process has already finished its work; the assertion is in upstream ImGui's OSX viewport-data destructor running during ImGui context teardown.
- **Why now**: Reproducible since M18 (the editor was introduced before M26a — this debt is pre-existing). Filed here so it gets fixed when M26b touches the editor lifecycle.
- **Cost if left**: Spurious noise on exit. Doesn't crash the run before the assertion. CI noise if assert-aborts are treated as failures.
- **Fix path**: Confirm upstream behaviour against latest ImGui (we currently pin a specific FetchContent SHA — bump it and retest). If still present, file upstream and either (a) carry a one-line patch in our fetch, or (b) suppress the assert in shutdown by clearing the platform handle before `ImGui::DestroyContext()`.
- **Owner**: editor
- **Created**: 2026-05-13

### [P3-M25C-NORMAL-MR-001] glTF demo samples base-color only — normal + MR maps unused
- **What**: M25c loads `GltfScene` via cgltf and uploads the active material's base-color texture, but `k_gltf_gbuffer_msl`'s fragment stage still uses the per-instance flat MR (`inst.mr.g` for roughness, `inst.mr.r` for metallic) and writes only the per-vertex normal into the G-Buffer. DamagedHelmet's `normalTexture` + `metallicRoughnessTexture` ride along in the parsed scene but never reach the GPU.
- **Why now**: M25c was scoped to prove the CLI + base-color path. Normal mapping needs either glTF vertex tangents (cgltf exposes them but we don't read `cgltf_attribute_type_tangent` yet) or derivative-based TBN reconstruction (cheaper, fewer codepaths, slightly worse on low-frequency surfaces). Metallic-roughness is trivial (single sample modulating `inst.mr`) but wants to ship alongside the normal-map work so the milestone slice covers a real PBR asset end-to-end.
- **Cost if left**: DamagedHelmet (and any real glTF) reads as a flat-shaded base-color decal — visually the M25c slice undersells the deferred PBR pipeline. RT reflections off the metallic spheres also can't pick up the helmet's metal/dielectric variation.
- **Fix path**: M25d. Either (a) extend `GltfVertex` to `GltfVertex+tangent` (back to 64 B, aligned), have `gltf_load.cpp` read `cgltf_attribute_type_tangent` (and generate it via MikkTSpace when missing), and add a TBN matrix in the vertex stage; or (b) compute TBN in the fragment via `dfdx/dfdy` against the UV + interpolated position. Then plumb a second + third texture binding (`normal_tex` → `[[texture(1)]]`, `mr_tex` → `[[texture(2)]]`) through the demo, modulate `inst.mr` with the sampled MR, sample-and-normalize the tangent-space normal, transform to world.
- **Owner**: renderer + assets
- **Created**: 2026-05-13

### [P3-M25C-MULTI-MESH-001] glTF demo consumes only `scene.meshes.front()`
- **What**: `DeferredRenderer::create` picks `gltf->meshes.front()` and its single base-color texture. Any glTF with multiple primitives or multiple materials shows only the first triangulated primitive; the rest of the parsed scene is dropped on the floor.
- **Why now**: M25c targets a "load a real .glb and see it render" slice. Multi-primitive support wants either multiple `gltf_gbuffer_pso` draws (one per primitive, each binding its own texture set) or a bindless texture array — both bigger than the M25c slice.
- **Cost if left**: Demo can't render multi-material assets (e.g. Khronos `FlightHelmet`, any character with separate body/clothing materials). The asset pipeline doesn't model material binding tables yet.
- **Fix path**: Phase 3 asset pipeline milestone. Build a `GltfRenderable` struct = `{vbuf, ibuf, base_color_tex, normal_tex, mr_tex, sampler}` and emit one draw per primitive in the gbuffer pass. Long-term lands inside the ECS proper as a `MeshRenderer` component.
- **Owner**: renderer + assets
- **Created**: 2026-05-13

### [P3-RT-REFIT-001] TLAS rebuilds in full every frame; refit not used
- **What**: `rebuild_dynamic_tlas()` calls `Device::build_acceleration_structure` for both the tube BLAS and the scene TLAS each frame. Allocates a fresh AS + scratch buffer each time; blocks the CPU until the GPU finishes.
- **Why now**: Refit (`MTLAccelerationStructureRefitOptions`) requires an existing AS allocated with `allowUpdate = YES` plus the same topology between rebuilds. Adding that to the RHI is its own slice of work, and at 1018 instances the full rebuild still hits ~3 ms — acceptable for the demo.
- **Cost if left**: Per-frame ~3 ms blocking on the queue. Drops the demo from 120 FPS → ~83 FPS when the dynamic TLAS is active. Scales linearly with instance count, so any real scene blocks the editor budget hard.
- **Fix path**: Add `AccelerationStructure` flags for `allowUpdate`. Expose `Device::refit_acceleration_structure(queue, AS, desc)` that re-uses the same physical AS + scratch buffers. Tube BLAS topology is constant (vertex count + index count never change) so refit is sufficient. TLAS refit also works since the instance count is fixed.
- **Owner**: rhi + renderer
- **Created**: 2026-05-12

### [P2-EDITOR-RELOAD-001] Shader Reload only validates compile, no live swap
- **What**: The Shader Reload panel's per-shader button re-invokes `Device::create_shader_from_msl` to confirm the source still compiles. It does NOT replace the active PSO bound on the live pipelines.
- **Why now**: Hot-swap requires a stable shader-by-name registry the engine can use to find every PSO that referenced the old shader, then re-build each PSO with the new module. Today the demo creates each PSO inline with a captured shader pointer, so there's no central registry.
- **Cost if left**: Reload doesn't actually let you edit a shader and see the change without restarting the demo.
- **Fix path**: M23.b. Introduce `engine/renderer/shader_registry/` with `{name → Shader*}` and `{shader_name → [pso_name]}` indexes. Reload swaps the shader, walks the index, rebuilds dependent PSOs, atomically replaces them.
- **Owner**: editor + renderer
- **Created**: 2026-05-12

### [P2-EDITOR-FONTS-VENDOR-001] Editor depends on macOS system fonts
- **What**: The editor loads SF Pro + SF Mono from `/System/Library/Fonts/`. Works perfectly on macOS but doesn't survive to the iOS / Vulkan / DX12 backends planned for Phase 4.
- **Why now**: Vendoring Inter + JetBrains Mono adds ~600 KB of TTFs to the repo and a FetchContent step. Not urgent on a macOS-only Phase 2.
- **Cost if left**: A cross-platform build either has to vendor TTFs or stub the editor.
- **Fix path**: FetchContent Inter + JBM from upstream GitHub releases. Or embed via `binary_to_compressed_c` so the TTFs ship inside the binary.
- **Owner**: editor
- **Created**: 2026-05-12

### [P1-SKIN-RT-001] Skinned tube is invisible to RT shadows + reflections — RESOLVED (M24, 2026-05-12)
- **Was**: M16's skinned tube renders through the deferred path but isn't in the TLAS. It gets no ray-traced shadow on the ground and isn't reflected by metallic spheres.
- **Fix**: M24's `rebuild_dynamic_tlas` rebuilds the tube BLAS from `tube_skinned_vbuf` (compute skinning output) each frame, then rebuilds the TLAS to include the tube instance. The lighting RT pass marks `blas_tube` resident via `use_fragment_acceleration_structure`. 1-frame lag because the build runs after `fg.execute()`; acceptable for sub-second motion.
- **Cost paid**: ~3 ms blocking per frame for the full rebuild — `P3-RT-REFIT-001` tracks the refit optimization.

### [P1-LOD-DISTANCE-001] LOD selection uses raw distance, not projected size
- **What**: `sphere_lod[i]` is picked from `length(camera - sphere)` against fixed thresholds (9 m, 18 m). Doesn't account for FOV / zoom: a 30°-FOV camera at 20 m sees a sphere bigger than a 90°-FOV camera at 10 m, but the LOD pick is identical.
- **Why now**: All-distance heuristic works for the static demo camera. Projected screen-space size is the correct metric but pulls in projection matrix math at LOD-select time.
- **Cost if left**: Misjudges LOD on zoomed-in scenes. Visible popping when changing FOV.
- **Fix path**: Compute `radius / distance * cot(half_fov_y) * (height_px / 2)`; pick LOD by pixel coverage.
- **Owner**: renderer
- **Created**: 2026-05-12

### [P1-LOD-RT-BLAS-001] RT uses high-LOD BLAS only
- **What**: M15 builds 3 sphere LOD meshes for rasterization but only one BLAS (LOD0) for ray queries. Reflections always sample the highest-detail geometry regardless of which mesh the camera sees rasterized.
- **Why now**: Per-LOD BLAS means the TLAS instance's `accelerationStructureIndex` needs to flip per LOD change. The TLAS is static today (P1-RT-STATIC-TLAS-001); both deferrals share the same fix path.
- **Cost if left**: Reflections show high-detail spheres even when the rasterized variant is low-poly. Visually inconsistent on grazing surfaces (silhouettes don't match).
- **Fix path**: Build 3 sphere BLAS, switch TLAS to dynamic, rewrite `accelerationStructureIndex` per frame.
- **Owner**: renderer
- **Created**: 2026-05-12

### [P1-HZB-NOCULL-001] HZB cull writes visibility but draws don't read it
- **What**: M14 v1 computes the per-instance visibility byte in `hzb_visibility_buf` and counts occluded cubes for the HUD, but the cube vertex shader still rasterizes every frustum-visible cube. No GPU time is actually saved.
- **Why now**: Real cull either needs a vertex-shader degenerate-clip path (small change, modest win) or a GPU-driven indirect-command path (big change, big win). Splitting M14 off here keeps the milestone tight while validating the HZB pipeline end-to-end.
- **Cost if left**: The HZB compute work runs but doesn't pay back. With ~10 % occluded cubes in our toy scene the upside is small; on dense scenes (city, dungeon) leaving this on the table is meaningful.
- **Fix path**: M14.b. First pass: cube vertex shader reads `hzb_visibility_buf[iid - cube_base]`, outputs a degenerate vertex if 0. Second pass (later): switch to `MTLIndirectCommandBuffer` + `executeCommandsInBuffer`.
- **Owner**: renderer
- **Created**: 2026-05-12

### [P1-HZB-SINGLEMIP-001] HZB has no mip pyramid
- **What**: M14 v1 uses a single 256² HZB texture. Large AABBs sample many texels per query. Phase 1.5 mip-based HZB would let each query sample one texel from the matching-resolution mip.
- **Why now**: Mip support needs either per-mip texture views (RHI API extension) or N separate textures (allocation noise). Single-resolution HZB ships with zero new RHI surface.
- **Cost if left**: Per-instance query cost is O(rect-area) instead of O(1). Cheap at our cube count, costly at 10k+.
- **Fix path**: Either add `Device::create_texture_view(tex, mip_range, slice_range)` + `ComputeEncoder::set_texture_view` (cleanest), or allocate one R32F texture per mip and bind them as an array.
- **Owner**: rhi
- **Created**: 2026-05-12

### [P1-RT-STATIC-TLAS-001] TLAS is static (built once at startup)
- **What**: M13 builds the scene TLAS once during `DeferredRenderer::build_tlas` and never updates it. Moving / rotating / spawning geometry won't be reflected in RT shadows or reflections. The M11 sphere wobble was removed for this reason.
- **Why now**: The blocking builder we have today would stall CPU on every frame to rebuild ~1k instances. Adding a non-blocking acceleration-structure command encoder + FrameGraph integration is M13.b scope.
- **Cost if left**: All dynamic scenes (animated transforms, streamed-in geometry) are locked out of RT.
- **Fix path**: Add `AccelStructEncoder` RHI type. Distinguish refit (cheap, requires unchanged topology) from rebuild. Wire into FrameGraph as a pre-frame pass when a TLAS is marked dirty.
- **Owner**: rhi + frame_graph
- **Created**: 2026-05-12

### [P1-RT-CSM-FALLBACK-001] CSM shadow pass still runs when RT is active
- **What**: When `rt_active` is true the lighting pass uses the RT lighting PSO and ignores the shadow map, but the `shadow` FG pass + 2048² Depth32Float shadow map still get built and rendered every frame.
- **Why now**: Kept as a fallback for `--no-rt` (and for platforms without RT support, when we get cross-platform).
- **Cost if left**: ~0.3 ms GPU + 16 MB VRAM wasted every frame the demo is RT.
- **Fix path**: Conditionally add the shadow pass to the FrameGraph only when `rt_active` is false. The `shadow_pso` / `shadow_sampler` can stay allocated.
- **Owner**: demo + frame_graph
- **Created**: 2026-05-12

### [P1-RT-HIT-MATERIAL-001] Reflection hit shading is approximate
- **What**: `lighting_rt_fs` reflection branch doesn't fetch the hit-point material or geometry — it uses a neutral `albedo=0.7` and approximates lighting with a sun-visibility check. So a metallic sphere reflects "the abstract world lit by sun" rather than "the actual cube that the ray hit".
- **Why now**: Real hit shading needs either a full RT pipeline (closest-hit shaders + shader binding table) or per-instance material lookup tables threaded into the inline ray query.
- **Cost if left**: Reflections look uniform; can't distinguish reflecting "blue cube" vs "pink cube".
- **Fix path**: Either (a) switch to a full RT pipeline with closest-hit functions, or (b) pack a per-instance material index into `MTL::AccelerationStructureInstanceDescriptor::userID`, build a global material buffer indexed by it, and look it up from the hit data.
- **Owner**: renderer/lighting
- **Created**: 2026-05-12

### [P1-PARTICLES-DEPTH-001] No depth test / soft particles
- **What**: Particle render pass has no depth attachment, so particles draw over scene geometry regardless of z. Visually fine for fire over the scene; wrong for particles meant to be occluded by cubes.
- **Why now**: Particle pass runs after tonemap (which writes to the backbuffer, not the HDR depth). Re-importing the gbuffer depth as a read texture is a small change but pulls in FG-buffer-handle thinking, so deferring.
- **Cost if left**: Particles always appear in front of geometry.
- **Fix path**: Read the gbuffer depth as a shader resource in the particle fragment shader; compare to the fragment's view-space depth; soften by `smoothstep`. Lands with M12.b.
- **Owner**: renderer/particles
- **Created**: 2026-05-12

### [P1-RHI-TEXUPLOAD-001] Font atlas upload bypasses RHI via metal-cpp escape hatch — RESOLVED (M25a, 2026-05-12)
- **Was**: `examples/hello_metal/main.cpp` used `MTL::Texture::replaceRegion` directly to upload the M11 font atlas. The RHI had no public upload API.
- **Fix**: `Texture::upload_region(mip, x, y, w, h, bytes, bytes_per_row)` lands on the RHI. Metal backend calls `replaceRegion` for Shared/Managed textures. Demo's font atlas still uses the escape hatch but the API is now available for the M25 glTF texture uploads.
- **Outstanding**: Private-storage textures still need a staging-buffer + blit-encoder path. Tracked as `P3-RHI-BLIT-001` for when we move font atlas / glTF maps to Private.
