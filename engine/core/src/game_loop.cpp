#include "mge/core/game_loop.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace mge::core {

void GameLoop::set_fixed_dt(float dt) noexcept {
    fixed_dt_ = std::max(1e-4f, dt);
}

void GameLoop::set_target_fps(float hz) noexcept {
    target_fps_ = std::max(1.0f, hz);
}

void GameLoop::set_time_scale(float s) noexcept {
    time_scale_ = std::max(0.0f, s);
}

void GameLoop::reset_timer() noexcept {
    prev_              = now();
    next_pace_target_  = prev_;
    accumulator_       = 0.0;
}

void GameLoop::tick(const SimFn& sim, const RenderFn& render) {
    const TimePoint t  = now();
    double          dt = seconds(t - prev_);
    prev_              = t;
    if (dt > static_cast<double>(max_real_dt_)) {
        dt = static_cast<double>(max_real_dt_);
    }
    run_steps_and_render(static_cast<float>(dt), sim, render);
    pace_to_target();
}

void GameLoop::tick_with_dt(float real_dt, const SimFn& sim, const RenderFn& render) {
    double dt = static_cast<double>(real_dt);
    if (dt > static_cast<double>(max_real_dt_)) dt = static_cast<double>(max_real_dt_);
    run_steps_and_render(static_cast<float>(dt), sim, render);
    // No pacing in deterministic mode.
}

void GameLoop::run_steps_and_render(float real_dt, const SimFn& sim, const RenderFn& render) {
    if (!paused_ && time_scale_ > 0.0f) {
        accumulator_ += static_cast<double>(real_dt) * static_cast<double>(time_scale_);
    }

    int steps = 0;
    while (accumulator_ >= static_cast<double>(fixed_dt_) && steps < max_steps_per_frame_) {
        if (sim) sim(fixed_dt_);
        accumulator_ -= static_cast<double>(fixed_dt_);
        sim_time_    += static_cast<double>(fixed_dt_);
        ++step_count_;
        ++steps;
    }

    // Spiral-of-death guard: if we still have a big surplus after capping
    // steps, drop the rest of the accumulator with a counter for visibility.
    if (accumulator_ >= static_cast<double>(fixed_dt_)) {
        const std::uint64_t skipped = static_cast<std::uint64_t>(
            accumulator_ / static_cast<double>(fixed_dt_));
        dropped_steps_ += skipped;
        accumulator_ = std::fmod(accumulator_, static_cast<double>(fixed_dt_));
    }

    last_steps_  = steps;
    last_alpha_  = (paused_ || time_scale_ == 0.0f)
                       ? 0.0f
                       : static_cast<float>(accumulator_ / static_cast<double>(fixed_dt_));
    if (render) render(last_alpha_);
    ++frame_count_;
}

void GameLoop::pace_to_target() {
    const auto target_dt = std::chrono::duration<double>(1.0 / static_cast<double>(target_fps_));
    next_pace_target_ += std::chrono::duration_cast<Duration>(target_dt);

    const TimePoint t = now();
    if (next_pace_target_ < t) {
        // We're behind schedule — snap target forward to avoid runaway drift.
        next_pace_target_ = t;
        return;
    }
    // Coarse sleep until ~250 us before target, then spin to be precise.
    constexpr auto kSpinThreshold = std::chrono::microseconds(250);
    const TimePoint sleep_until   = next_pace_target_ - kSpinThreshold;
    if (sleep_until > t) {
        std::this_thread::sleep_until(sleep_until);
    }
    while (now() < next_pace_target_) {
        // busy-wait for the last sub-millisecond
    }
}

}  // namespace mge::core
