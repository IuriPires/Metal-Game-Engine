#pragma once

#include "mge/core/time.h"

#include <cstdint>
#include <functional>

namespace mge::core {

// Glenn Fiedler's accumulator pattern with decoupled render and frame pacing.
// See ADR-0006 and docs/GAME_LOOP.md.
//
//   while (running) {
//       loop.tick(
//           [&](float dt)    { sim.step(dt); },     // 60 Hz fixed
//           [&](float alpha) { render.draw(alpha); } // uncapped, paced to target_fps
//       );
//   }
//
// Simulation advances in discrete `fixed_dt` steps. The render callback
// receives `alpha ∈ [0, 1)`, the blend factor between the most-recently-
// completed sim state and the next one — use it to interpolate visuals.
class GameLoop {
public:
    using SimFn    = std::function<void(float dt)>;
    using RenderFn = std::function<void(float alpha)>;

    GameLoop() noexcept { reset_timer(); }

    // Sim tick rate (default 60 Hz).
    void set_fixed_dt(float dt) noexcept;

    // Render pacing target (default 120 fps). The pacer sleeps + spin-loops to
    // hit this rate when the platform isn't already enforcing vsync.
    void set_target_fps(float hz) noexcept;

    // Pause stops sim from consuming time. Render still ticks (overlay UI etc).
    void set_paused(bool p) noexcept { paused_ = p; }

    // 0 = paused, 1 = normal, 0.5 = slow-mo, 2.0 = fast-forward. Clamped to >=0.
    void set_time_scale(float s) noexcept;

    // Clamp on real_dt to prevent spiral-of-death after long stalls.
    void set_max_real_dt(float s) noexcept { max_real_dt_ = s; }
    void set_max_steps_per_frame(int n) noexcept { max_steps_per_frame_ = n; }

    [[nodiscard]] float         fixed_dt()           const noexcept { return fixed_dt_; }
    [[nodiscard]] float         target_fps()         const noexcept { return target_fps_; }
    [[nodiscard]] bool          is_paused()          const noexcept { return paused_; }
    [[nodiscard]] float         time_scale()         const noexcept { return time_scale_; }
    [[nodiscard]] double        sim_time()           const noexcept { return sim_time_; }
    [[nodiscard]] std::uint64_t step_count()         const noexcept { return step_count_; }
    [[nodiscard]] std::uint64_t frame_count()        const noexcept { return frame_count_; }
    [[nodiscard]] int           last_steps()         const noexcept { return last_steps_; }
    [[nodiscard]] float         last_alpha()         const noexcept { return last_alpha_; }
    [[nodiscard]] std::uint64_t dropped_steps()      const noexcept { return dropped_steps_; }

    // Standard tick: measures real_dt from a monotonic clock and drives one
    // outer iteration. Calls sim 0..N times, then render once. Paces to the
    // target fps before returning.
    void tick(const SimFn& sim, const RenderFn& render);

    // Test/determinism variant: caller provides real_dt directly. No pacing
    // sleep. Identical inputs produce identical sim_time / step_count.
    void tick_with_dt(float real_dt, const SimFn& sim, const RenderFn& render);

    // Reset the accumulator and the internal clocks. Call after window
    // minimization or other long stalls.
    void reset_timer() noexcept;

private:
    void run_steps_and_render(float real_dt, const SimFn& sim, const RenderFn& render);
    void pace_to_target();

    float    fixed_dt_              = 1.0f / 60.0f;
    float    target_fps_            = 120.0f;
    float    max_real_dt_           = 0.25f;
    int      max_steps_per_frame_   = 5;
    bool     paused_                = false;
    float    time_scale_            = 1.0f;

    TimePoint prev_              = now();
    TimePoint next_pace_target_  = now();
    double    accumulator_       = 0.0;
    double    sim_time_          = 0.0;

    std::uint64_t step_count_    = 0;
    std::uint64_t frame_count_   = 0;
    std::uint64_t dropped_steps_ = 0;
    int           last_steps_    = 0;
    float         last_alpha_    = 0.0f;
};

}  // namespace mge::core
