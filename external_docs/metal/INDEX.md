# Metal Documentation Cache

Local mirror of Apple's Metal docs for offline reference. The repo also includes the `metal-cpp` source archive as a backup copy of what `FetchContent` will pull in M1.

## Files

| File | Source | SHA256 | Notes |
|------|--------|--------|-------|
| `Metal-Shading-Language-Specification.pdf` | https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf | `eed87a82d4d2d475423b91b3c529c5313a85433f83e22b7fe3ec50e90254f44a` | Full MSL specification (~12 MB). Read sections on resource binding, argument buffers, function constants, tile shading, and ray tracing. |
| `Metal-Feature-Set-Tables.pdf` | https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf | `cbedcb0689c60da32069eba4f9461910404850bd872e26ef968ac2a32df61fe2` | Per-GPU-family capability tables (Apple1..Apple9, Mac1..Mac2). Cross-reference any feature gating. |
| `metal-cpp_26.4.zip` | https://developer.apple.com/metal/cpp/files/metal-cpp_26.4.zip | `4e7587c0f351d782f1f3e7c67c649b40d0ba780c051b2f6729f00ddc61ada28c` | Apple's official C++ binding. Same archive `third_party/` will FetchContent at M1. |

## Online references (not cached — visit when needed)

- Metal documentation hub — https://developer.apple.com/documentation/metal/
- Metal Performance Shaders — https://developer.apple.com/documentation/metalperformanceshaders/
- Metal Performance Shaders Graph — https://developer.apple.com/documentation/metalperformanceshadersgraph/
- Metal Ray Tracing (MPS / Metal 3) — https://developer.apple.com/documentation/metal/ray_tracing
- WWDC sessions — https://developer.apple.com/wwdc25/, /wwdc24/, /wwdc23/ — filter on Metal / Graphics
- Discover Metal 4 — https://developer.apple.com/metal/Discover-Metal-4.pdf (if mirrored later)
- Sample code — https://developer.apple.com/documentation/metal/sample_code

## Reading order for newcomers to Metal

1. **TBDR architecture** — Apple GPU is tile-based deferred. Understand tile memory, programmable blending, and threadgroup tile shading before designing render passes. See `research/apple-tbdr-architecture.md`.
2. **Resource binding** — argument buffers Tier 2 are mandatory for our bindless design. MSL spec, section on argument buffers.
3. **Command buffer lifecycle** — `MTLCommandBuffer`, `MTLCommandEncoder`, presentation. Sync primitives: `MTLEvent`, `MTLSharedEvent`, `MTLFence`.
4. **Heaps & aliasing** — `MTLHeap` for our `GpuHeap`/transient pool aliasing.
5. **Ray tracing** — `MTLAccelerationStructure`, intersection functions, intersection tester. Apple's `MTLDevice` ray-tracing capabilities by family.

## Cache policy

- Files in this directory are committed for reproducibility, but kept small (< 20 MB each).
- Update by re-downloading via curl + recomputing SHA. Update this table.
- Apple periodically rotates URLs — keep both the file and the URL it came from.
