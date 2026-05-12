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

- **P1-OBJC-001** — Obj-C++ shim at platform edge: resolved in M1. See entry above and ADR-0001.

## New debt opened in M1

### [P1-METAL-001] metal-cpp framework-search workaround
- **What**: We pass metal-cpp's include path without the `metal-cpp/` directory prefix so that `<Foundation/Foundation.hpp>` resolves through Apple Clang's framework lookup miss-then-fall-through. Fragile if Apple ever changes Clang's lookup order.
- **Why now**: It works today on Apple Clang 17 and unblocks M1.
- **Cost if left**: A future toolchain bump could break the include path; we'd detect it immediately on CI.
- **Fix path**: If it breaks, pre-create a symlink directory inside the build tree (`${binary_dir}/metal-cpp-wrapper/metal-cpp -> ${metal_cpp_SOURCE_DIR}`) and add the wrapper to the include path. Documented in ADR-0001.
- **Owner**: renderer/metal
- **Created**: 2026-05-12

### [P1-WINDOW-001] No window resize handling yet
- **What**: `mge::platform::Window` does not react to user-driven resize. The CAMetalLayer's `drawableSize` stays at construction values.
- **Why now**: Demo doesn't need it; the resize event plumbing wants a real input/event system, which lands in M4.
- **Cost if left**: Resizing the demo window produces stretched / cropped output. Not crash-worthy.
- **Fix path**: NSWindowDelegate's `windowDidResize:` + `Swapchain::resize` call. Move to M4 alongside the camera controller.
- **Owner**: platform
- **Created**: 2026-05-12

## New debt opened in M3

### [P1-RHI-MOCK-001] No mock RHI backend yet
- **What**: Frame-graph / pass-level unit tests cannot run without a real Metal device. Currently all GPU coverage is in the integration suite.
- **Why now**: M3 budget didn't include a full mock; deferring was the right call.
- **Cost if left**: CI on non-Metal runners (Linux) cannot run RHI tests. Frame-graph TDD pace at M5 will suffer until mock lands.
- **Fix path**: Build `mge_rhi_mock` at M5, link from a dedicated `mge_rhi_unit_tests` target.
- **Owner**: renderer/rhi
- **Created**: 2026-05-12

### [P1-RHI-SHADERS-001] Shaders are inline string literals, runtime-compiled
- **What**: MSL source for every pipeline is a `constexpr const char*` inside the consumer .cpp. Runtime compile via `MTL::Device::newLibrary` on every startup.
- **Why now**: Shader pipeline tooling (offline metallib, hot reload, reflection) is M5/M6 territory.
- **Cost if left**: Slow startup as scenes grow, no compile-time validation in CI.
- **Fix path**: Offline shader compiler tool under `tools/shader_compiler/` produces a `.metallib`; ship with the binary. Hot reload via file watcher in Debug. Pencil for M5.
- **Owner**: renderer/metal
- **Created**: 2026-05-12

### [P1-RHI-DRAW-001] RenderEncoder hard-codes TriangleList for now
- **What**: `RenderEncoder::draw` ignores `RenderPipeline::topology()` and always passes `MTL::PrimitiveTypeTriangle`.
- **Why now**: TriangleList is the only consumer in M3. Adding the lookup added test surface without a customer.
- **Cost if left**: M4+ may want strips / lines / points and hit this immediately.
- **Fix path**: Cache topology in encoder per `set_pipeline` call, use it in `draw`. Easy fix.
- **Owner**: renderer/metal
- **Created**: 2026-05-12
