# Apple GPU Architecture — TBDR

> Notes from WWDC sessions and Apple's Metal Best Practices guide. Apple GPUs are NOT immediate-mode renderers; the rendering model differs in ways that change how we should structure passes.

## TBDR in one paragraph

**Tile-Based Deferred Rendering**. The GPU bins draw commands by screen tile (typically 32×32 px). Within a tile, primitives are sorted front-to-back via a hidden-surface-removal step **before** fragment shading runs, so fragments occluded by later geometry never execute. Tile color/depth/stencil lives in **on-chip tile memory** during a render pass — only resolved out to main memory at pass end. This makes overdraw nearly free, depth/blending cheap, and storeless attachments a real optimization.

## Critical implications for our engine

### 1. Storeless attachments win

If a render-pass attachment is *only* used within that pass (e.g. intermediate depth, intermediate color in a multi-pass effect), we should set **store action = don't care** on Metal. The data never touches main memory. The frame graph (M5) makes this discoverable: an output that is read only by the next pass and never sampled afterwards is a perfect candidate.

### 2. Tile shading lets deferred lighting fit in one render pass

Apple lets us do this in one Metal render pass:
- Render G-Buffer (color attachments stay in tile memory).
- Run **tile shader** or programmable-blending step reading those tile attachments.
- Output final lit color to the final attachment.

If both G-Buffer and lit-color fit in tile memory simultaneously, we never round-trip to VRAM. This is a huge bandwidth win — sometimes 30-50 % over a naive deferred pipeline.

Constraint: tile memory budget is limited (~32 KB/tile on M-series). Thin G-Buffer is mandatory to fit.

Plan: ship "naive deferred" (G-Buffer → lighting as two passes) at M6 first, then refactor to tile-resident deferred in Phase 1.5 once we have a working baseline to compare against.

### 3. Programmable blending

Inside a fragment shader, we can read the **current tile color** at our pixel (not arbitrary sampling — same pixel only) via `[[color(N)]]` input attachments. Useful for custom blending and compositing without separate passes. Cheap on TBDR.

### 4. Memoryless storage

`MTLStorageMode::Memoryless` for textures that never leave tile memory (G-Buffer in a tile-resident deferred chain). Zero VRAM allocation. Frame graph must expose this as a resource property.

### 5. Discard / kill is OK

Unlike immediate-mode GPUs where `discard` can ruin early-Z, Apple GPUs do hidden surface removal differently. Fragment kills don't break HSR efficiency, though we still want to keep fragment shaders cheap.

### 6. MSAA is cheap on TBDR

MSAA samples live in tile memory and resolve in-place. The bandwidth tax of MSAA in immediate-mode GPUs largely disappears. **But** — we are using deferred, where MSAA semantics are awkward (G-Buffer has no concept of subsamples). TAA remains our primary anti-aliasing choice.

### 7. Render encoders are heavyweight

Each `MTLRenderCommandEncoder` corresponds to a render pass + its tile work. Splitting work across many encoders forces tile memory flushes. Goal: **minimize render passes** per frame, batch as much as possible into a single pass.

This is one more reason to push the lighting pass into the G-Buffer pass via tile shading.

### 8. Argument buffers Tier 2 are mandatory for bindless

On Apple GPUs, Tier 2 argument buffers (Metal 3) give us heap-of-resources bindless texturing. Our material system (M6) uses one persistent argument buffer for the material array and one per-frame argument buffer for globals.

### 9. Resource heaps + aliasing are free at the API level

`MTLHeap` lets us sub-allocate buffers and textures with the same back-end memory; the frame graph (M5) drives the placement.

### 10. Counter sample buffers for GPU timestamps

`MTLCounterSampleBuffer` with stage boundaries gives us reliable GPU-side timings without extra encoder overhead. This is what M11 hooks for the on-screen profiler.

## What we should ignore (or treat as caveat)

- **Tile shaders are not portable**. The tile-resident deferred path is an Apple optimization; Vulkan/DX12 backends will use a normal deferred chain. The frame graph must abstract this — same logical "deferred lighting" pass, different physical scheduling.
- **Apple GPU families differ**. Apple7 (A14/M1) vs Apple9 (A17 Pro/M3+) differ in ray tracing support, dynamic per-thread state, and tile sizes. Feature-gate at runtime; don't bake decisions at compile time.
- **`waitUntilCompleted` cripples performance**. Never call it on a render command buffer in steady state.

## Reading list

- WWDC22 "Discover Metal 3" — generally.
- WWDC22 "Maximize your Metal app performance".
- WWDC23 "Bring your game to Mac, part 2: Compile shaders and convert".
- WWDC24 / WWDC25 — sessions on Metal performance, ray tracing, tile shading.
- Apple "Metal Best Practices" guide (online docs).
- Imagination Technologies whitepapers on TBDR — Apple's GPU lineage.

## Tracking

Open items that affect Phase 1 decisions:

- [ ] Confirm tile memory budget on M1/M2/M3/M4 — used to gate G-Buffer slot count.
- [ ] Confirm Apple9 ray tracing instruction set — gates Phase 1.5 RT design.
- [ ] Confirm whether programmable blending requires `[[color(N)]]` declaration or only attachment binding — affects PSO setup.
