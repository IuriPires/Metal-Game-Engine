# Testing Strategy

## Test pyramid

```
                  ┌──────────────────────┐
                  │     Perf gates       │   slow, run on CI gating
                  └──────────────────────┘
                ┌──────────────────────────┐
                │  Golden image diffs      │   end-to-end render
                └──────────────────────────┘
              ┌──────────────────────────────┐
              │     Integration tests        │   multi-subsystem
              └──────────────────────────────┘
        ┌──────────────────────────────────────────┐
        │              Unit tests                  │   fast, ubiquitous
        └──────────────────────────────────────────┘
```

## Tooling

- **Unit + integration**: doctest (header-only, fast compile).
- **Rendering goldens**: PNG diff with a perceptual metric (FLIP preferred; fallback ΔE2000 + tolerance).
- **Perf gates**: custom timer harness that records p50/p99/p99.9 and asserts against a baseline JSON.
- **Profiling**: Tracy zones for CPU; Metal counter sample buffers for GPU.

## What must have tests in Phase 1

| Subsystem | Unit | Integration | Golden | Perf |
|-----------|------|-------------|--------|------|
| Math | ✅ (invariants, matrices, quaternions) | – | – | – |
| Memory allocators | ✅ (alignment, exhaustion, reset, free-list) | – | – | ✅ (1M ops) |
| Result/Logger/Assert | ✅ | – | – | – |
| Time/Clock | ✅ | – | – | – |
| RHI (mock backend) | ✅ (resource transitions, encoder usage) | – | – | – |
| RHI (Metal backend) | – | ✅ (offscreen triangle) | ✅ (triangle PNG) | – |
| Frame graph | ✅ (topo sort, aliasing) | ✅ (two-pass render) | – | – |
| GBuffer pass | – | ✅ | ✅ | ✅ |
| Deferred lighting | – | ✅ | ✅ (furnace) | ✅ |
| CSM | – | ✅ | ✅ | ✅ |
| Post FX | – | ✅ | ✅ per stage | ✅ |
| Cull / instancing | ✅ (frustum AABB cases) | – | – | ✅ (50k cubes) |
| Game loop | ✅ (accumulator) | ✅ (replay determinism) | – | ✅ (pacing) |

## Conventions

- Unit tests live in `tests/unit/`, named `test_<subsystem>_<aspect>.cpp`.
- Integration tests live in `tests/integration/`.
- Golden fixtures live in `tests/golden/fixtures/`. Reference images live in `tests/golden/ref/`. Actual runs write to `tests/golden/_actual/` and diffs to `tests/golden/_diff/` (both gitignored).
- Perf tests live in `tests/perf/`, name `perf_<subsystem>.cpp`. Baseline JSON committed under `tests/perf/baselines/`.
- CI runs unit + integration on every push; golden + perf on push to `main` and on demand.

## Determinism

- Sim replay test: 10 000 frames, same input log, same seed → state hash equality.
- Render goldens: fixed scene, fixed camera, fixed seed. Animated scenes use deterministic frame indexes.

## Visual debugging

- All passes can be skipped or replaced by a debug variant.
- Debug visualizations: depth, normals, motion vectors, cluster IDs, cascade IDs, allocator pressure.
- Frame capture via `MTLCaptureManager` invokable from a debug menu (Phase 1.5).

## CI matrix (target)

| Platform | Preset | Tests | Required to merge |
|----------|--------|-------|--------------------|
| macOS-14 arm64 | debug | unit + integration | ✅ |
| macOS-14 arm64 | release | unit + integration | ✅ |
| macOS-14 arm64 | asan | unit + integration | ✅ |
| macOS-14 arm64 | profile | perf | informational, no blocking |

## See also

- `docs/TECH_DEBT.md`
- `docs/STYLE_GUIDE.md`
