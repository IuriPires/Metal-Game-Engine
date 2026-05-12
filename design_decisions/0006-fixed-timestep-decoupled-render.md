# ADR-0006 — Fixed-timestep simulation with decoupled render

- **Status**: accepted
- **Date**: 2026-05-12
- **Tags**: gameplay, simulation, render, threading

## Context

The engine needs to support:
- Stable visuals at high refresh (120 Hz target on M-series).
- Deterministic simulation (replay tests, future networking).
- Smooth motion regardless of render rate.
- Pause and time-scale.
- Bounded behavior under stall / drift (no "spiral of death").

## Decision

Use **Glenn Fiedler's accumulator pattern** with a **fixed simulation tick** (60 Hz by default, configurable) and a **decoupled render** thread that **interpolates** between the latest two sim snapshots.

Concretely:

- Sim thread runs `sim.step(FIXED_DT)` in a loop driven by an accumulator fed with real time scaled by `time_scale`.
- Sim writes triple-buffered snapshots; render reads the latest two with blend factor `alpha ∈ [0,1)`.
- Render thread runs as fast as the frame pacer allows (target 120 Hz).
- A **spiral-of-death guard** clamps `real_dt` and caps steps per real frame.

## Alternatives considered

### Variable-timestep sim (one sim step per render frame, sim_dt = real_dt)

- **Pros**: simplest.
- **Cons**: non-deterministic, physics blow up at low fps, replays impossible.
- **Why it lost**: blocks determinism and networking goals.

### Fixed-timestep, sim and render lock-step (no interpolation)

- **Pros**: simpler than the accumulator pattern.
- **Cons**: visual stutter when render rate ≠ sim rate; cannot show smooth 120 fps from a 60 Hz sim.
- **Why it lost**: kills the 120 Hz visual experience.

### Semi-fixed timestep (clamped variable dt with substeps)

- **Pros**: middle ground.
- **Cons**: not deterministic.
- **Why it lost**: same blocker as variable.

## Consequences

- Sim is **pure** with respect to its inputs (events + fixed_dt + state). Reproducible.
- Render reads a const snapshot; it cannot mutate sim state.
- Pause = `time_scale = 0`, no sim steps but render keeps running.
- Slow-mo / fast-fwd are trivial.
- Spiral-of-death cannot occur (clamped real_dt + cap on steps/frame); excess time is dropped with a warning.

## Tradeoffs

- **Latency**: input → visual latency is bounded by `≤ 1 sim tick + 2 render frames` worst-case.
- **Bandwidth**: triple-buffered sim state — extra memory proportional to active state size (acceptable: a few MB at most).
- **Determinism**: full same-binary same-machine; cross-machine is Phase 2+.

## Implementation notes

- `engine/core/time.h` — clock, time point, duration.
- `engine/core/game_loop.h` — `GameLoop` type, accumulator, time scale, pause.
- `engine/ecs/sim_snapshot.h` — ring buffer of 3 snapshots.
- Render reads via `Snapshot::interpolate(prev, next, alpha)`.

Test plan:

- Unit: accumulator state machine over varied `real_dt` sequences.
- Determinism replay: 10k frames, two runs, hash equality.
- Pacing: empty render at 120 Hz target, frame-time stddev < 0.5 ms p99.

## Open questions

- Sim tick frequency configurable per game; default 60 Hz, but UI / netcode games may prefer 30/120.
- Whether sim runs on its own thread from M1 or rolls in at M10. Plan says M10.

## References

- Glenn Fiedler, "Fix Your Timestep!" — https://gafferongames.com/post/fix_your_timestep/
- Jonathan Blow, "How to do Frame Rate Independent Physics in a Game Loop".
