# Game Loop

## Goals

- Stable 120 FPS render target on Apple Silicon.
- Deterministic simulation independent of render rate.
- Smooth visuals via interpolation of simulation state.
- Bounded frame-pacing variance (<0.5 ms p99 at vsync-off).
- Pause and time-scale built in, not bolted on.

## Architecture

**Glenn Fiedler's accumulator pattern** with **decoupled render**:

```cpp
constexpr float FIXED_DT = 1.0f / 60.0f;          // 60 Hz sim
constexpr float MAX_STEPS_PER_FRAME = 5;          // anti-spiral-of-death

TimePoint prev = clock.now();
float accumulator = 0.0f;

while (running) {
    const TimePoint now = clock.now();
    const float real_dt = std::min(seconds(now - prev), 0.25f);  // clamp big stalls
    prev = now;

    accumulator += real_dt * time_scale;

    int steps = 0;
    while (accumulator >= FIXED_DT && steps < MAX_STEPS_PER_FRAME) {
        sim.step(FIXED_DT);                       // pure / deterministic
        accumulator -= FIXED_DT;
        ++steps;
    }

    const float alpha = accumulator / FIXED_DT;   // [0,1) blend factor
    render.draw(interpolated_state(alpha));

    platform.pace();                              // 1/120s budget if vsync off
}
```

Key properties:
- **Deterministic** sim: same input log + same fixed seed = same state. Verified by determinism replay test.
- **Render does not move state**. Render reads a snapshot; sim writes a snapshot.
- **`alpha` interpolation** smooths visuals between sim ticks.
- **Spiral-of-death guard**: clamp `real_dt` and cap steps per frame. Excess time is dropped (with a warning log).
- **Pause**: `time_scale = 0` → no sim steps run, but render keeps drawing the last snapshot (editor and UI stay alive).
- **Slow-mo / fast-forward**: `time_scale ∈ (0, ∞)` scales the accumulator.

## Threading

```
┌──────────────────────────┐    SimSnapshot[ring]    ┌───────────────────────────┐
│   Sim thread (60 Hz)     │ ───────────────────────►│  Render thread (≤120 Hz)  │
│  fixed-timestep stepper  │                         │   interpolates + records  │
└──────────────────────────┘                         └──────────────┬────────────┘
                                                                    │ command buffers
                                                                    ▼
                                                          ┌────────────────────┐
                                                          │  MTLCommandQueue   │
                                                          └──────────┬─────────┘
                                                                     │
                                                                     ▼
                                                          ┌────────────────────┐
                                                          │        GPU         │
                                                          └────────────────────┘
```

- **Main thread** pumps the platform run loop (window, input).
- **Sim thread** owns the accumulator and writes ring-buffered snapshots (3 slots).
- **Render thread** picks `snapshot[N-1]` and `snapshot[N]` plus `alpha` and submits a frame.
- **Worker pool** handles culling, mip generation, command-list fan-out.

Per-frame state is **triple-buffered** so the CPU can be up to 2 frames ahead of the GPU.

## Frame pacing

When vsync is off (uncapped):

- Target frame time = `1 / target_fps_hz` (default 120 Hz → 8.333 ms).
- Sleep with `mach_wait_until` until `target_time - 250µs`, then **busy-spin** the last 250 µs for sub-millisecond precision.
- Measure realized frame time; adjust next-frame target by half the error (PI controller-lite) to avoid drift.

When vsync is on:
- `CAMetalDrawable` carries a presentation deadline. Schedule submission so GPU work completes before deadline.

Pacing variance metric: standard deviation of frame times over a 5-second window. Reported on overlay.

## Determinism

In **deterministic mode** (used by the replay test, network rollback, recordings):

- `real_dt` ignored; the loop drives `accumulator` from an input log.
- Float math uses fixed evaluation order (no parallel reduction in sim).
- Random number sources use seeded PRNGs (xoshiro256**), thread-local.
- Anything that depends on system time, true RNG, or thread scheduling is forbidden in sim code.

Limitation: cross-machine determinism (different CPUs/compilers) is **not** a Phase 1 goal. Same-binary same-machine only. Documented as tech debt.

## Pause / time scale API

```cpp
class TimeSystem {
    void set_paused(bool);
    bool is_paused() const;
    void set_time_scale(float);     // 0 = paused, 1 = normal, 2 = fast-fwd, 0.5 = slow-mo
    float time_scale() const;

    // Independent "real" time for editor UI that ignores pause.
    Duration real_dt() const;
    Duration scaled_dt() const;
};
```

## Tests

- **Unit**: accumulator math (steps consumed for varying real_dt), spiral-of-death clamp, interpolation alpha edge cases.
- **Integration**: run sim+render headless for 10 000 frames; assert frame-time histogram is bounded.
- **Determinism replay**: 10 000-frame input log → state hashes match across two runs of the same binary.
- **Perf**: pacing variance < 0.5 ms p99 on Apple Silicon with empty render.

## See also

- ADR-0006 (fixed timestep + decoupled render)
- `docs/THREADING_MODEL.md`
- Glenn Fiedler, "Fix Your Timestep" (https://gafferongames.com/post/fix_your_timestep/)
