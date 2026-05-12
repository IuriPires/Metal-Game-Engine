# Threading Model

## Threads (Phase 1)

| Thread | Role | Notes |
|--------|------|-------|
| Main | Window / input pump | macOS pins UI work here. Lightweight. |
| Sim | Fixed-timestep simulation (60 Hz) | Deterministic, writes snapshots. |
| Render | Builds frame graph + records command buffers | Reads sim snapshots. Submits to `MTLCommandQueue`. |
| Worker × N | General job pool | N = `hw_threads − 3`, min 1. |
| Asset I/O × 2 | Disk + glTF/texture decode | Phase 1: minimal, M4 only. |

## Communication

- **Sim → Render**: triple-buffered snapshot ring. Atomic publish of "latest committed index".
- **Render → GPU**: `MTLCommandBuffer` submission. GPU completion drives the frame-allocator reset.
- **Workers ↔ Render**: per-frame `JobHandle`s, joined at pass boundaries.
- **Input → Sim**: lock-free SPSC queue of input events.

No locks in hot paths except the threadpool's task queue (lock-free MPMC deque).

## Job system (Phase 1, minimal)

Goals: enough parallelism for culling fan-out and command-list recording. Not a Phase 1 goal: fibers, work-stealing prioritization beyond simple FIFO, multi-queue scheduling.

```cpp
namespace mge::jobs {
    using JobFn = std::function<void()>;
    struct JobHandle { /* opaque */ };

    void init(unsigned num_workers);
    void shutdown();

    JobHandle submit(JobFn);
    JobHandle parallel_for(std::size_t count, std::size_t grain, std::function<void(std::size_t,std::size_t)>);
    void wait(JobHandle);
    void wait_all();
}
```

Tech debt: `std::function` allocs. Phase 2 swaps to a small-buffer-optimized callable. Logged in `docs/TECH_DEBT.md`.

## Determinism

Sim runs single-threaded in Phase 1. The accumulator and step function are pure with respect to a deterministic PRNG. Parallelism inside sim is deferred to Phase 2+, where it requires deterministic reductions.

## Synchronization with GPU

- **Fences**: `MTLSharedEvent` for cross-queue and CPU↔GPU.
- **Frame-in-flight**: 3 frames. Render thread waits on the `MTLSharedEvent` for "frame N − 3 done" before reusing frame N's resources.
- **No `waitUntilCompleted` in steady state** — only on shutdown.

## Bad patterns

- Touching `MTL*` objects from multiple threads without explicit handoff.
- Allocating from a heap visible to multiple threads without external sync (Metal docs).
- Inflating worker count past `hw_threads − 1`. We reserve cores for main + sim + render.
- Spawning OS threads ad hoc. Always use the pool.

## Tests

- **Unit**: job pool submit/wait, parallel_for grain correctness.
- **Stress**: 1M tiny jobs, no leaks, deterministic completion order under `wait_all`.
- **TSan**: full ASan/TSan presets in CI.

## Future work

- Work-stealing scheduler with per-thread deques.
- Fibers for deep callstacks (Naughty Dog / Frostbite style).
- Priority lanes (low-latency render path vs. background asset path).
- GPU-driven dispatch using indirect compute (Phase 2+).

## See also

- ADR-0006
- `docs/MEMORY_MODEL.md`
- `docs/GAME_LOOP.md`
