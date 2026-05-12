# Metal Game Engine

A modern, high-performance AAA-style game engine in C++20, built from scratch.

- **Primary platform**: macOS / Apple Silicon
- **Primary GPU backend**: Metal (Metal-cpp, pure C++)
- **Planned backends**: iOS, Vulkan, DirectX 12

## Status

Phase 1 in progress — renderer foundation + game loop. See [`docs/ROADMAP.md`](docs/ROADMAP.md).

## Layout

| Path | Purpose |
|------|---------|
| `engine/` | Engine subsystems (core, math, memory, renderer, platform, jobs, …) |
| `shaders/metal/` | Metal Shading Language sources |
| `tests/` | Unit, integration, golden image, perf tests |
| `tools/` | Build/asset tooling |
| `docs/` | Architecture, rendering, game-loop, threading docs |
| `design_decisions/` | ADRs |
| `research/` | Distilled notes from books in `refs/` |
| `external_docs/metal/` | Cached Apple Metal documentation |
| `refs/` | Source reference books (user-provided, read-only) |
| `examples/` | Demo scenes |

## Build

Prerequisites: Xcode 15+ command line tools, CMake ≥ 3.27, Ninja.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Run the M1 demo (open a Metal-backed window, clear-color animated, frame timing):

```sh
./build/debug/examples/hello_metal/hello_metal --frames 240
```

`--headless` runs a Metal device smoke test without opening a window (used in CI).

For Metal GPU debugging via Xcode:

```sh
cmake --preset xcode
open build/xcode/MetalGameEngine.xcodeproj
```

## Documentation

Start with [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md), then `docs/RENDERING_PIPELINE.md` and `docs/GAME_LOOP.md`.

ADRs (Architecture Decision Records) live in [`design_decisions/`](design_decisions/).

## Conventions

C++20, `clang-format`, `clang-tidy`. See [`docs/STYLE_GUIDE.md`](docs/STYLE_GUIDE.md).
