# ADR-0002 — CMake + Ninja as primary build system

- **Status**: accepted
- **Date**: 2026-05-12
- **Tags**: build, tooling

## Context

We need a build system that:
- Works well on macOS today with Metal and Apple Silicon.
- Will work on iOS, Windows (DX12), and Linux (Vulkan) in the future.
- Plays nicely with `clangd`, IDE indexers, and CI.
- Generates Xcode projects on demand for the Metal GPU debugger.

## Decision

Use **CMake ≥ 3.27** with **Ninja** as the primary generator. Provide an **Xcode** generator preset for Metal frame capture and Instruments usage only.

## Alternatives considered

- **Xcode-only project**: tied to one IDE, fragile in CI, hostile to cross-platform.
- **Bazel**: hermetic and scales beautifully, but the Apple/Metal ecosystem (frameworks, code-signing, CAMetalLayer setup) is rough. Curve too steep for the project size.
- **Meson**: good but smaller ecosystem; no compelling advantage over CMake.
- **Hand-rolled make/build scripts**: short-term win, long-term losing.

## Consequences

- Standard ergonomics: `cmake --preset debug && cmake --build --preset debug && ctest --preset debug`.
- `compile_commands.json` is exported for `clangd` / IDE indexers.
- `CMakePresets.json` defines `debug`, `release`, `profile`, `asan`, `xcode` consistently.
- We accept CMake's quirks and verbosity in exchange for portability.

## Tradeoffs

- **Velocity**: neutral. CMake is verbose but well-known.
- **CI**: positive — every macOS runner has Ninja or can install it in one line.
- **Cross-platform**: positive — same CMake works for Vulkan/DX12 backends later.
- **GPU debugging**: handled via the `xcode` preset on demand.

## Implementation notes

- Minimum CMake: 3.27 (for `CMakePresets.json` v6, `add_subdirectory` with system include juggling).
- Use `target_link_libraries(... PRIVATE mge_warnings mge_sanitizers ...)` to centralize flags.
- All third-party deps are pulled via `FetchContent` with pinned tags/commits.

## References

- https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html
