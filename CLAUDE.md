# CLAUDE.md — Metal Game Engine

This file is the operating manual for AI agents (including me, future-Claude) working in this repo. Read it first every session.

---

## Mission

Build a modern AAA-style game engine in C++20 targeting Apple Silicon / Metal first, with future Vulkan/DX12 backends. Phase 1 = renderer foundation + game loop. See [`docs/ROADMAP.md`](docs/ROADMAP.md) for milestone state.

## Locked decisions

These were confirmed by the user at project start and should NOT be relitigated without explicit user input:

1. **Metal binding**: Metal-cpp (pure C++). No `.mm` files except possibly at the AppKit window/swapchain edge if a critical API is missing — and only then with an ADR entry.
2. **Build**: CMake ≥ 3.27 + Ninja. Xcode generator is a side preset used for Metal GPU debugger / Instruments only.
3. **Math**: Custom SIMD math, NEON intrinsics on Apple Silicon, SoA-friendly. ABI must match Metal Shading Language packed types.
4. **Phase 1 scope**: Vertical slice — RHI, frame graph, deferred PBR, CSM, HDR/ACES/bloom/TAA, frustum culling, GPU instancing, decoupled game loop. Ray tracing, particles, occlusion culling GPU, LOD, skeletal animation are explicitly Phase 1.5 — do not pull them into Phase 1 without a green light.
5. **Language**: C++20, no exceptions in hot paths (use `Result<T,E>` / `std::expected`-style return values). No RTTI in render code.
6. **Testing**: doctest for unit tests, golden-image diff for rendering, perf tests with frame budget gates. All systems must ship with tests.

## Source of truth

- Architecture: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
- Rendering pipeline: [`docs/RENDERING_PIPELINE.md`](docs/RENDERING_PIPELINE.md)
- Game loop: [`docs/GAME_LOOP.md`](docs/GAME_LOOP.md)
- Memory: [`docs/MEMORY_MODEL.md`](docs/MEMORY_MODEL.md)
- Threading: [`docs/THREADING_MODEL.md`](docs/THREADING_MODEL.md)
- Roadmap / milestones: [`docs/ROADMAP.md`](docs/ROADMAP.md)
- Tech debt: [`docs/TECH_DEBT.md`](docs/TECH_DEBT.md)
- Known issues: [`docs/KNOWN_ISSUES.md`](docs/KNOWN_ISSUES.md)
- Testing strategy: [`docs/TESTING_STRATEGY.md`](docs/TESTING_STRATEGY.md)
- Style: [`docs/STYLE_GUIDE.md`](docs/STYLE_GUIDE.md)
- ADRs: [`design_decisions/`](design_decisions/)

When a decision is taken, **write an ADR**. Format in `design_decisions/ADR-template.md`.

## How to build

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

Other presets:

- `release` — optimized, Tracy off
- `profile` — `RelWithDebInfo`, Tracy on
- `asan` — debug + ASan + UBSan
- `xcode` — Xcode generator for Metal frame capture / Instruments

## Workflow for a new subsystem

Before writing implementation code, this sequence must produce artifacts:

1. **Research note** in `research/` if external prior art / book chapters are relevant.
2. **ADR** in `design_decisions/` covering: context, decision, alternatives, consequences, tradeoffs.
3. **Doc update** in the relevant `docs/*.md` describing the subsystem's contract.
4. **Test plan** — what unit/integration/golden tests will assert correctness.
5. **Implementation** behind RHI if it touches the GPU.
6. **Profiling hooks** (Tracy zones, GPU timestamps).
7. **Tech-debt entry** if anything is left for later.

Never skip steps 1-4. The engine has to be defensible architecturally first.

## Coding conventions

- C++20, `-Wall -Wextra -Wpedantic -Wconversion`. See `cmake/Warnings.cmake`.
- No `using namespace` in headers. Engine root namespace is `mge`.
- No global singletons in renderer/sim code. Pass dependencies explicitly.
- Zero hidden allocations in hot paths. Use arenas / pools / preallocated pools.
- Prefer `static_assert`, `constexpr`, and `consteval` where applicable.
- `assert`s are `MGE_ASSERT` (Debug) and `MGE_VERIFY` (kept in Release).
- Headers `.h`, sources `.cpp`, inline templates `.inl`.
- Files ≤ 800 lines. Functions ≤ 60 lines preferred.
- Use `[[nodiscard]]` on factory and result-returning functions.

## Don't

- Don't add abstractions speculatively. RHI is the only pre-emptive abstraction allowed in Phase 1; everything else justifies itself with a concrete need.
- Don't introduce exceptions in render code. Use `Result<T,E>`.
- Don't hide GPU work behind sugar. Make resource lifetimes and synchronization explicit.
- Don't commit binary assets larger than 5MB without ADR.
- Don't pull in dependencies casually. Each new dep needs an ADR.
- Don't break the API of a shipped milestone without a deprecation note.

## When in doubt

1. Re-read the relevant `docs/*.md`.
2. Check `design_decisions/` for prior ADRs.
3. Check `research/` for distilled prior art.
4. Ask the user.

## Repo orientation map

| Subsystem | Path | Owner doc |
|-----------|------|-----------|
| Core (Log/Assert/Time/Result) | `engine/core/` | ARCHITECTURE.md |
| Math (SIMD vec/mat/quat) | `engine/math/` | ARCHITECTURE.md |
| Memory (arenas, pools) | `engine/memory/` | MEMORY_MODEL.md |
| Platform (window, input, swapchain) | `engine/platform/macos/` | ARCHITECTURE.md |
| Job system | `engine/jobs/` | THREADING_MODEL.md |
| Profiling | `engine/profile/` | TESTING_STRATEGY.md |
| RHI | `engine/renderer/rhi/` | RENDERING_PIPELINE.md |
| Metal backend | `engine/renderer/metal/` | RENDERING_PIPELINE.md |
| Frame graph | `engine/renderer/frame_graph/` | RENDERING_PIPELINE.md |
| Shader system | `engine/renderer/shader_system/` | RENDERING_PIPELINE.md |
| Passes | `engine/renderer/passes/` | RENDERING_PIPELINE.md |
| PBR | `engine/renderer/pbr/` | RENDERING_PIPELINE.md |
| Shadows | `engine/renderer/shadows/` | RENDERING_PIPELINE.md |
| Post FX | `engine/renderer/postfx/` | RENDERING_PIPELINE.md |
| Culling | `engine/renderer/culling/` | RENDERING_PIPELINE.md |
| Instancing | `engine/renderer/instancing/` | RENDERING_PIPELINE.md |
| Camera | `engine/renderer/camera/` | RENDERING_PIPELINE.md |
| Assets (glTF, textures) | `engine/assets/` | ARCHITECTURE.md |
| ECS stub | `engine/ecs/` | ARCHITECTURE.md |

## Sanity checks before declaring work "done"

- Build green under all enabled presets locally (`debug` and `release` minimum).
- `ctest --preset debug` green.
- Relevant `docs/*.md` updated.
- ADR present if a non-trivial decision was made.
- Tech debt entry added if anything was deferred.
- No new TODOs without a corresponding line in `docs/TODO.md`.
