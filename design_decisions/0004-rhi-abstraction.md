# ADR-0004 — RHI abstraction layer

- **Status**: accepted
- **Date**: 2026-05-12
- **Tags**: renderer, architecture

## Context

The engine has one GPU backend today (Metal) and three on the roadmap (iOS Metal, Vulkan, DX12). Two extreme positions:

1. **No abstraction**: code directly against Metal-cpp; port later by rewriting.
2. **Full abstraction now**: complete RHI mirroring Vulkan-style explicit GPU.

Both are wrong:
- (1) makes future backends a rewrite, and prevents mockable unit tests.
- (2) burns engineering time on abstractions before we know which Metal idioms generalize.

## Decision

Build a **thin RHI** now. Names mirror modern explicit APIs (device, queue, command buffer, command encoder, pipeline state, buffer, texture, sampler, fence, query). The Metal backend is the only implementation today. RHI types are concrete handles with backend-private opaque pointers — not virtual interfaces — to avoid v-table cost on the hot path.

Render-technique code (passes, materials, frame graph) talks to the RHI only.

## Alternatives considered

### Virtual-interface RHI

- **Pros**: clean OO separation.
- **Cons**: virtual dispatch in record-command paths is measurable; harder to inline; heavier ABI.
- **Why it lost**: explicit handle + opaque-impl pattern (used by every shipped engine RHI) gives us the same separation without the v-table cost.

### Skip RHI now, refactor later

- **Pros**: short-term velocity.
- **Cons**: every renderer subsystem accumulates direct Metal calls; refactor cost grows non-linearly with surface area.
- **Why it lost**: we already know we want a second backend; build the seam now while there is one call-site to migrate.

## Consequences

- Renderer code expresses intent, not Metal idioms.
- Unit tests of frame graph and pass logic can run against a mock RHI backend on CI without a GPU.
- Adding Vulkan/DX12 later is a backend implementation, not a renderer rewrite.
- We accept some surface drift early — RHI v1 will mirror Metal more than Vulkan. Cleaned up when a second backend is introduced.

## Tradeoffs

- **Cost (early)**: ~3–5 days of design for v1.
- **Cost (medium)**: small — every render addition needs an RHI mirror.
- **Performance**: negligible — calls are batched at the command-list level.
- **Portability**: massive positive.

## Implementation notes

- `engine/renderer/rhi/`:
  - `device.h`, `queue.h`, `command_buffer.h`, `encoder.h`
  - `buffer.h`, `texture.h`, `sampler.h`
  - `pipeline.h`, `shader.h`, `bind_group.h`
  - `fence.h`, `query.h`
  - `format.h`, `enums.h`
- `engine/renderer/metal/`: Metal-cpp impl behind opaque pointers in the RHI types.
- Test mock backend: `tests/unit/mock_rhi/`.

## Open questions

- Lockable vs. lock-free command buffer recording across threads — designed in M3.
- Bind groups: argument-buffer-Tier-2 only path vs. dynamic-binding fallback for Vulkan compatibility — designed at M6 or Phase 4 start.

## References

- Frostbite "FrameGraph" presentations (Yuriy O'Donnell).
- The Forge / Filament / Bevy / O3DE RHI sources for prior art.
