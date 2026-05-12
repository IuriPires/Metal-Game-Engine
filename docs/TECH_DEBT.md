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

### [P1-OBJC-001] Possible Obj-C++ shim at platform edge
- **What**: Some macOS APIs (e.g., NSApplicationDelegate callbacks, drag-drop) may need `.mm` glue.
- **Why now**: Metal-cpp is pure C++, but AppKit is not.
- **Cost if left**: Minor — bounded surface, easy to maintain.
- **Fix path**: If any `.mm` lands, it must be confined to `engine/platform/macos/` and described in ADR-0001 follow-up.
- **Owner**: platform
- **Created**: 2026-05-12

---

## Closed debt

(none yet)
