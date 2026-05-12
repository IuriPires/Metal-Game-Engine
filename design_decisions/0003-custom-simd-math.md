# ADR-0003 — Custom SIMD math library (NEON-first)

- **Status**: accepted
- **Date**: 2026-05-12
- **Tags**: math, performance, abi

## Context

The engine needs a math library for vectors, matrices, quaternions, bounding volumes, and frustum math. Options:

1. **GLM** — header-only, mature, GLSL-like ABI.
2. **Apple `simd.h`** — Apple's `simd_float4x4` etc.; same types usable in MSL via includes.
3. **Eigen** — heavyweight, expression templates, overkill for engine math.
4. **Custom** — purpose-built for Apple Silicon, NEON-first, SoA-friendly, ABI matched to MSL.

We also need:
- Tight control over alignment (16 / 32 / 64 bytes) for batch SoA processing in cull / animation / particles.
- Stable ABI with MSL so we can share matrices across the C++ ↔ MSL boundary without packing surprises.
- Deterministic evaluation order in sim code paths.

## Decision

Implement a **custom math library** under `engine/math/`. Use **NEON intrinsics** on Apple Silicon with a scalar fallback. Match the layout of MSL's `packed_float3`, `float4`, `float3x3`, `float4x4` byte-for-byte. Verify with `static_assert` on `sizeof` and `alignof`.

## Alternatives considered

### GLM

- **Pros**: battle-tested, familiar GLSL-like API.
- **Cons**: not optimized for NEON, OOP-ish overloads can hide costs, mixing precision is awkward, no first-class SoA helpers.
- **Why it lost**: engine math is a foundational layer; we want to control ABI and SoA paths directly.

### Apple `simd.h`

- **Pros**: shares types with MSL out of the box, excellent codegen on Apple Silicon.
- **Cons**: ties us to Apple's headers, makes future Vulkan/DX12 ports clunky, limited SoA support.
- **Why it lost**: forecloses portability. We do, however, mirror its ABI in our types so MSL <-> C++ remains friction-free.

### Eigen

- **Pros**: powerful.
- **Cons**: large, slow to compile, optimized for linear algebra not 3D engine math, no SoA pipeline.
- **Why it lost**: not the right tool for hot-path engine math.

## Consequences

- We own the math layer end-to-end. Bugs, but also full control over performance and ABI.
- Need thorough unit tests for invariants (identity, transpose, inverse round-trip, quat ↔ matrix round-trip).
- Need conformance tests vs. a reference (e.g., GLM in test-only builds) for confidence during early implementation.

## Tradeoffs

- **Cost**: ~2 days to get vec/mat/quat solid; another ~1 day for AABB/plane/frustum.
- **Performance**: significant positive — bespoke NEON in cull and animation hot paths.
- **Portability**: math layer is pure C++; intrinsics live behind an `arch/` partition.

## Implementation notes

- `engine/math/`:
  - `vec.h`, `vec.inl`
  - `mat.h`, `mat.inl`
  - `quat.h`, `quat.inl`
  - `aabb.h`, `plane.h`, `frustum.h`
  - `arch/neon.h`, `arch/scalar.h`
- All types `final`, trivially copyable, `constexpr` where possible.
- `static_assert(sizeof(Mat4) == sizeof(matrix_float4x4))` etc.

## Open questions

- Do we expose AoSoA helpers (e.g., `Vec3x4` for 4-wide lanes)? Likely yes for cull; design at M8.

## References

- *Real-Time Rendering, 4e* — math foundations.
- Apple's `simd_float4x4` reference (we mirror its layout).
