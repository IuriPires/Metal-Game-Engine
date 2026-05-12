# TODO

Catch-all for small in-flight items that don't belong in `ROADMAP.md` or `TECH_DEBT.md`. Promote to roadmap or tech debt once they grow teeth.

## Engineering

- [ ] Decide config format (TOML vs JSON) → ADR-0008
- [ ] Pick logging sink interface (sinks vs callback list)
- [ ] Pick string formatter (`fmt` vs `std::format` once Apple Clang catches up)
- [ ] Pick allocator-aware container set (`pmr` vs custom)
- [ ] Decide on GPU timestamp resolution policy
- [ ] Pick image diff metric for golden tests (FLIP vs ΔE2000 vs MSE+ψ)
- [ ] Pick perf gate harness (Catch2 benchmark vs custom timer harness)

## Tooling

- [ ] Set up GitHub Actions macOS-14 arm64 CI
- [ ] Add `compile_commands.json` symlink target to repo root for editors
- [ ] Add `tools/frame_capture/` MTLCaptureManager wrapper
- [ ] Add `tools/shader_compiler/` offline metallib builder
- [ ] Add pre-commit hook for clang-format

## Docs

- [ ] Move `Physically Based Rendering.epub` at repo root into `refs/` (verify it's the same file)
- [ ] Add diagrams (Excalidraw or Graphviz) for: frame graph DAG, threading model, memory layout

## Research

- [ ] Distill RTR4 ch12 (image-space) into `research/`
- [ ] Distill RTR4 ch7 (shadows) into `research/`
- [ ] Distill PBRT ch13 (Monte Carlo) into `research/`
- [ ] Distill Apple WWDC sessions on tile-based deferred → `research/apple-tbdr-architecture.md`

## Phase 1 stretch

- [ ] PCSS instead of plain PCF on cascades
- [ ] HDR display detection + RGB10A2 swapchain
- [ ] GPU memory introspection in overlay
