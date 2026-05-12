# ADR-0007 — Deferred renderer as the primary opaque path

- **Status**: accepted
- **Date**: 2026-05-12
- **Tags**: renderer, pbr, lighting

## Context

Modern PBR renderers commonly pick between:

1. **Forward+** (clustered/tiled forward shading) — single pass, easy MSAA, easy transparency, but shading work duplicates if overdraw is high.
2. **Deferred** (G-Buffer + screen-space lighting) — decouples geometry submission from lighting, scales well with light count, MSAA is awkward.
3. **Visibility Buffer** — newer, GPU-driven, more complex to implement, best for very high triangle counts.
4. **Hybrid** — deferred opaque + forward+ transparent (the de facto AAA standard).

The engine targets:
- Many lights per scene.
- Apple TBDR architecture, which actually favors tile-resolved deferred-on-tile patterns.
- Complex materials, but mostly under a unified PBR BRDF.
- Future ray-traced shadows / reflections that plug into G-Buffer derivatives.

## Decision

Use a **hybrid** pipeline:

- **Opaque pass**: deferred. Thin G-Buffer.
- **Transparent pass**: Forward+ using the same cluster grid as deferred lighting.
- **Future**: ray-traced shadows + reflections inject themselves between G-Buffer and lighting as alternate frame-graph passes.

Visibility-buffer-style GPU-driven rendering stays on the roadmap (Phase 2+) once the foundation is solid.

## Alternatives considered

### Forward+ only

- **Pros**: simpler, MSAA friendly, good for translucent scenes.
- **Cons**: shading cost scales with overdraw; per-pixel material variation is more expensive.
- **Why it lost**: doesn't differentiate our pipeline vs. trivial implementations and gives up the screen-space lighting decoupling we want.

### Deferred only

- **Pros**: clean, predictable shading cost.
- **Cons**: transparency is awkward without OIT; OIT itself is expensive on M-series.
- **Why it lost**: hybrid wins by adding a Forward+ transparency pass at almost no extra design cost.

### Visibility buffer

- **Pros**: scales to insane triangle counts, low overshading.
- **Cons**: complex; needs mesh shaders or compute culling to be worth it; ecosystem still maturing.
- **Why it lost in Phase 1**: not now. We'll re-evaluate after M10.

## Consequences

- G-Buffer layout becomes a contract: changes require coordinated updates across many passes.
- Motion vectors live in the G-Buffer from M6 (not bolted on at M9), which makes TAA integration painless.
- Forward+ transparency pass shares the cluster grid with deferred opaque lighting — single light culling pass, two consumers.

## G-Buffer layout (Phase 1)

| Slot | Format | Contents |
|------|--------|----------|
| 0 | RGBA8 Unorm | Albedo (RGB) + AO (A) |
| 1 | RG16 Snorm + RG8 | Normal (octahedral, 2× 16-bit) + Roughness + Metallic |
| 2 | RGBA8 Unorm | F0 (RGB) + Material ID (A) |
| 3 | RG16 Float | Motion vectors |
| Depth | D32 Float | Reverse-Z |

The exact packing of slot 1 may shift; the contract is "compact thin G-Buffer".

## Tradeoffs

- **Memory**: ~16–20 bytes per pixel of G-Buffer + depth at 1440p ≈ ~140 MB. Acceptable on M-series with unified memory.
- **Bandwidth**: TBDR mitigates G-Buffer bandwidth via on-tile reads when lighting fits in tile memory. We aim to keep deferred lighting tile-resident where possible (single subpass-style chain in v2).
- **Complexity**: middle ground. Two pipelines (deferred + forward+), one cluster cull.

## Open questions

- Whether to convert lighting into a true single-subpass tile-shaded pass on Apple GPUs in Phase 1.5. The TBDR architecture invites this. Plan: keep deferred lighting as a normal fullscreen quad in v1; revisit at M9.
- Material ID byte routes to a small material LUT or to material-permuted shader variants — design at M6.

## References

- *Real-Time Rendering, 4e* — Chapter 20 (graphics pipelines), Chapter 9 (PBR).
- Apple WWDC sessions on TBDR + Programmable Blending.
- "Practical Clustered Shading" — Olsson et al.
