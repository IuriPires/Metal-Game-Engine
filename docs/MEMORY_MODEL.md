# Memory Model

## Principles

1. **Explicit ownership**. Every allocation has a named arena/pool/heap.
2. **Frame-local memory** is the default for ephemeral work.
3. **Zero hidden allocations** in render and sim hot paths. Debug builds assert this.
4. **Cache-friendly layouts**. SoA where lanes are processed together.
5. **GPU memory is first-class** — managed via `MTLHeap` and frame-graph aliasing.

## CPU allocators

### Arena (bump)

```cpp
class Arena {
public:
    Arena(std::byte* buffer, std::size_t bytes);
    void* allocate(std::size_t bytes, std::size_t align);
    template <class T, class... Args> T* emplace(Args&&...);
    void reset();   // O(1), frees everything
    std::size_t bytes_used() const;
};
```

Use for: per-frame scratch, parse buffers, transient command-list records.

### Pool

Fixed-size object slots with a free-list. O(1) allocate/free, no fragmentation.

Use for: renderables, materials, lights, descriptor handles.

### FrameAllocator

Ring of 3 arenas indexed by `frame_index % 3`. Reset happens at frame begin (only when the GPU is done with that slot).

Use for: per-frame uniform blocks, dynamic vertex/index data, command-list records that live one frame.

### Heap allocator (general purpose)

`std::pmr::synchronized_pool_resource` wrapper for long-lived objects that are not pool-sized. All allocator-aware containers use a polymorphic allocator with this resource by default.

## GPU memory

### `GpuHeap`

Sub-allocator on top of `MTLHeap`. Supports placement of buffers and textures with alignment guarantees.

```cpp
class GpuHeap {
    GpuHeap(MTL::Device&, std::size_t bytes, MTL::StorageMode);
    GpuAllocation allocate(const MTL::SizeAndAlign&);
    void deallocate(GpuAllocation);
    Stats stats() const;
};
```

- **Persistent heap**: long-lived textures, vertex/index buffers, IBL probes.
- **Transient heap**: per-frame attachments, aliased by the frame graph.
- **Upload heap**: shared/streamed memory for CPU→GPU staging.

### Frame-graph aliasing

The frame graph computes a virtual lifetime for each transient resource and emits placement instructions: any two resources whose lifetimes don't overlap may occupy the same physical bytes.

Result: dramatic VRAM reduction vs naive allocation, especially for post-FX chains.

### Bindless / argument buffers

Metal Argument Buffers (Tier 2) hold the descriptor sets for material textures and global LUTs. Persistent material table is one large argument buffer; per-frame globals are a transient one.

## Tracking & visibility

Every allocator has:

```cpp
struct Stats {
    std::size_t bytes_in_use;
    std::size_t bytes_high_water;
    std::size_t bytes_capacity;
    std::size_t num_live_allocs;
    std::size_t num_alloc_calls;
    std::size_t num_free_calls;
};
```

Surfaces:
- Overlay (M11) — top-line numbers.
- Tracy — per-allocator zones + memory plots.
- Crash dump — last allocator state for forensics.

## Forbidden patterns

- `new` / `delete` outside of explicit allocators in render or sim code.
- `std::vector` resize inside hot loops without a `reserve` upfront.
- `shared_ptr` for GPU resources. GPU lifetimes are explicit (frame graph or persistent heap).
- Hidden allocations in logging, formatting, or assertion paths in Release.

## Tests

- **Unit**: arena alignment, exhaustion behavior, reset correctness; pool reuse and free-list integrity.
- **Stress**: 1M allocate/free cycles, randomized sizes/alignments.
- **GPU heap**: aliasing correctness — two non-overlapping resources end up at the same offset, one overlapping pair does not.

## Future work (not Phase 1)

- Resource streaming with priority queues.
- Hot/cold field splitting for archetype storage.
- ARC-style GPU lifetime ref counts for assets shared across scenes.

## See also

- ADR-0005 (frame graph) — aliasing is owned by the graph.
- `engine/memory/`
