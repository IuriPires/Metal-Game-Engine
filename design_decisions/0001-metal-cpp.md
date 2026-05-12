# ADR-0001 — Metal-cpp as the Metal binding

- **Status**: accepted
- **Date**: 2026-05-12
- **Deciders**: project owner, principal graphics engineer
- **Tags**: renderer, platform, language

## Context

The engine targets Metal as the primary backend. There are three ways to talk to Metal from C++:

1. **Metal-cpp** — Apple's official C++ headers wrapping Objective-C Metal via opaque handles and inline trampolines.
2. **Objective-C++ (`.mm`)** — write `.mm` files that mix C++ and Objective-C messaging.
3. **A hybrid** — pure C++ everywhere except a thin `.mm` shim on the platform edge (window, CAMetalLayer setup).

The engine codebase needs to stay portable for future Vulkan / DX12 backends and remain readable as a unified C++ codebase.

## Decision

Use **Metal-cpp** for all Metal interactions. Permit a single optional `.mm` translation unit at the platform edge (`engine/platform/macos/`) only if a critical AppKit / CAMetalLayer API is not reachable from pure C++.

## Alternatives considered

### Objective-C++ everywhere

- **Pros**: more samples online, idiomatic Apple style.
- **Cons**: encourages mixing styles across the engine, harder to lint/format, leaks Objective-C concepts (selectors, autorelease pools, retain/release) into engine code, larger compile times.
- **Why it lost**: violates the "one language for the engine" principle and complicates a future Vulkan/DX12 backend that has no business knowing about Objective-C.

### Hybrid

- **Pros**: gives us an escape hatch for stubborn AppKit APIs.
- **Cons**: in practice the Metal-cpp surface is sufficient for everything except the AppKit window itself.
- **Why it's the implicit fallback**: we keep the hybrid option open at the platform edge only, with a doc-level requirement that any `.mm` file is justified in this ADR's revision history.

## Consequences

- Engine code is uniformly C++20.
- Tooling (clang-format, clang-tidy, IDE indexers) works cleanly.
- Future Vulkan/DX12 backends can be added behind the same RHI without an Objective-C dependency.
- We accept that some samples we read online need translation when copying patterns.
- Metal-cpp lags Apple's public Obj-C headers by 1–2 SDK releases on edge features. We accept this risk; any blocked feature gets a sub-ADR.

## Tradeoffs

- **Velocity (early)**: slight negative — fewer copy-paste-ready samples.
- **Velocity (medium)**: positive — uniform style, no language-boundary friction.
- **Performance**: zero impact — Metal-cpp inlines trampolines into the call site.
- **Portability**: significant positive.

## Open questions

- ~~Decide whether to write our own NSWindow wrapper in C++ via the Objective-C runtime, or accept a tiny `.mm` shim. Revisit at M1.~~
  **Resolved (M1, 2026-05-12)**: a tiny `.mm` shim is the right call. `engine/platform/macos/src/app.mm` and `window.mm` are the only Obj-C++ in the engine (~150 LOC total). They expose pure C++ APIs (`mge::platform::App`, `mge::platform::Window`) so the rest of the codebase never sees Obj-C. Tech-debt entry `P1-OBJC-001` is now resolved.

## Known integration gotchas

- **metal-cpp include path collides with Apple Clang's framework search.** `<Foundation/Foundation.hpp>` would normally make Clang try `Foundation.framework/Headers/Foundation.hpp`, which does not exist, and Clang refuses to fall back to user-include paths even with `-I`. Workaround: place the metal-cpp directory directly on the include path (no `metal-cpp/` prefix) and use angle-bracket form. The framework lookup still happens but Clang then continues to user paths because `Foundation.hpp` is not in the framework. Verified in `third_party/CMakeLists.txt`.
- **metal-cpp triggers `-Wgnu-anonymous-struct` and `-Wsign-conversion` warnings** under our strict baseline. Mitigated by marking the include path as `SYSTEM`.
- **Private implementation macros** (`MTL_PRIVATE_IMPLEMENTATION` etc.) must be defined in exactly one TU. We put them in `engine/renderer/metal/src/metal_cpp_impl.cpp`.

## Implementation notes

- Vendor `metal-cpp` and `metal-cpp-extensions` from Apple's official drop into `third_party/metal-cpp/`.
- Make `Metal.framework`, `MetalKit.framework`, `Foundation.framework`, `QuartzCore.framework`, `AppKit.framework` link dependencies of the platform target.
- Provide a `metal-cpp` CMake target with appropriate include paths and framework links.

## References

- https://developer.apple.com/metal/cpp/
- https://developer.apple.com/metal/cpp/Metal-cpp-beginners-guide-1.pdf
