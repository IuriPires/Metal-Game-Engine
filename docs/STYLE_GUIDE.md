# Style Guide

## Language

- **C++20**. No C++23 features yet — Apple Clang availability still uneven.
- **No exceptions** in render or sim code. Use `Result<T,E>` / `std::expected` for fallible paths.
- **No RTTI** in render code. Use tag-based discriminators where polymorphism is needed.
- **MSL** for shaders; shared headers in `shaders/metal/common/`.

## Formatting

`clang-format` config lives in `.clang-format`. Pre-commit hook enforces it.

Key choices:
- 4-space indent, 110-column soft limit.
- Attach braces.
- Left-aligned `*` and `&`.
- Includes regrouped: project, third-party, system.

## Naming

| Kind | Convention | Example |
|------|------------|---------|
| Namespace | `lower_case` | `mge::renderer` |
| Class / Struct / Enum | `PascalCase` | `FrameGraph`, `Vec3` |
| Function / Method | `lower_case` | `bind_pipeline()` |
| Variable / Member | `lower_case` | `frame_index` |
| Private member suffix | `_` | `device_` |
| Macro | `UPPER_SNAKE` | `MGE_ASSERT` |
| Constant | `lower_case` | `pi`, `max_lights` |
| Template parameter | `PascalCase` | `template <class T>` |

## Header hygiene

- One class per header where reasonable.
- Always include what you use. No relying on transitive includes.
- Forward-declare in headers; full include in `.cpp`.
- No `using namespace` in headers.
- Headers `.h`, sources `.cpp`, inline templates `.inl`.
- Include order: project (`engine/...`), third-party (`<tracy/...>`, `<doctest/...>`), system (`<vector>`).

## Files

- 200–400 lines typical, **800 max**.
- Functions ≤ 60 lines preferred; pure utility functions ≤ 20.
- Cyclomatic complexity ≤ 12.

## Error handling

```cpp
mge::Result<Pipeline, RhiError> create_pipeline(const PipelineDesc& desc);

if (auto pso = device.create_pipeline(desc); pso.is_ok()) {
    use(pso.value());
} else {
    log_error("pipeline failed: {}", pso.error());
    return pso.error();
}
```

- Hot paths: no logging at >warn level, no allocations.
- Cold paths: rich error context.
- Assertions: `MGE_ASSERT(cond, "message")` (Debug-only), `MGE_VERIFY(cond, "...")` (kept in Release for invariants you cannot afford to remove).

## Comments

- Prefer well-named identifiers and types over comments.
- Comments explain **why**, not **what**.
- Keep TODO comments paired with an entry in `docs/TODO.md` (or a tech-debt ID).

## Memory

- No `new` / `delete` outside of allocators in render or sim code.
- Containers default to PMR allocators with the appropriate memory resource.
- Use `reserve()` for `std::vector` whenever the upper bound is known.

## Logging

- One `LogCategory` per subsystem, defined in the subsystem's main header.
- Format strings are `fmt::format`-compatible.
- Per-frame log volume is bounded; rate-limit warnings using a token-bucket helper in `core/log.h`.

## Threading

- Don't roll your own threads. Use `mge::jobs::submit(...)`.
- Don't call `std::this_thread::sleep_for(...)` in non-pacing code.
- Touching Metal objects from multiple threads requires explicit handoff.

## Shaders (MSL)

- Use `[[buffer(N)]]`, `[[texture(N)]]`, `[[sampler(N)]]` index constants from a shared header.
- Vector/matrix types match the C++ side via shared header (`shaders/metal/common/types.h`).
- Avoid dynamically branching uniform reads in tight loops; prefer specialization constants.
- No `discard` in early Z-eligible passes.

## Commit messages

```
<type>: <subject>

<body, optional, wrapped at 72 cols>
```

Types: `feat`, `fix`, `refactor`, `docs`, `test`, `chore`, `perf`, `ci`.

## Reviews

- New subsystem → must have an ADR.
- New dependency → must have an ADR.
- Renderer API changes → must update `docs/RENDERING_PIPELINE.md`.
- Threading-relevant changes → must update `docs/THREADING_MODEL.md`.

## See also

- `.clang-format`, `.clang-tidy`
- `docs/TESTING_STRATEGY.md`
