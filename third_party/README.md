# third_party

External dependencies, vendored via CMake `FetchContent`.

## Policy

1. Every new dependency requires an ADR justifying its inclusion.
2. Versions are pinned by commit or release tag. SHA256 hashes added for downloaded archives.
3. No `find_package(...)` on system packages. Builds must be reproducible from a clean checkout.
4. Deps are consumed lazily — declared in `third_party/CMakeLists.txt` but only `MakeAvailable`-d once a milestone needs them.

## Current deps

| Dep | Version | Milestone | Status |
|-----|---------|-----------|--------|
| doctest | 2.4.11 | M0 | ✅ active |
| fmt | 10.2.1 | M2 | declared, not yet active |
| metal-cpp | macOS14.2/iOS17.2 | M1 | declared, not yet active (hash TBD) |
| cgltf | 1.14 | M4 | declared, not yet active |
| stb | latest | M3 | declared, not yet active |
| Tracy | 0.11.0 | M2/M11 | declared, not yet active |

## Adding a dep

1. Write an ADR (`design_decisions/NNNN-<name>.md`).
2. Add a `FetchContent_Declare` block to `third_party/CMakeLists.txt`.
3. Update this table.
4. Keep the `MakeAvailable` call commented until the milestone that consumes it begins.
