#include "mge/core/game_loop.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

using mge::core::GameLoop;

namespace {

// Drive `loop` with a fixed sequence of real_dt values and capture the sim_time
// trace each tick. Useful for both correctness and determinism checks.
std::vector<double> drive(GameLoop& loop, const std::vector<float>& dts) {
    std::vector<double> trace;
    trace.reserve(dts.size());
    for (float dt : dts) {
        loop.tick_with_dt(dt, [](float) {}, [](float) {});
        trace.push_back(loop.sim_time());
    }
    return trace;
}

}  // namespace

TEST_CASE("GameLoop consumes accumulator into discrete fixed steps") {
    GameLoop loop;
    loop.set_fixed_dt(1.0f / 60.0f);

    // 16.67 ms of real_dt -> one fixed step at 60 Hz.
    loop.tick_with_dt(1.0f / 60.0f, [](float) {}, [](float) {});
    CHECK(loop.last_steps() == 1);
    CHECK(loop.step_count() == 1u);

    // Less than fixed_dt accumulates without stepping.
    loop.tick_with_dt(0.005f, [](float) {}, [](float) {});
    CHECK(loop.last_steps() == 0);
    CHECK(loop.step_count() == 1u);
}

TEST_CASE("GameLoop time_scale slows / speeds the sim") {
    GameLoop a, b;
    a.set_time_scale(1.0f);
    b.set_time_scale(0.5f);

    // 1 second of real time -> 60 sim ticks at scale 1, 30 at scale 0.5.
    for (int i = 0; i < 60; ++i) {
        a.tick_with_dt(1.0f / 60.0f, [](float) {}, [](float) {});
        b.tick_with_dt(1.0f / 60.0f, [](float) {}, [](float) {});
    }
    CHECK(a.step_count() == 60u);
    CHECK(b.step_count() == 30u);
}

TEST_CASE("GameLoop paused does not step") {
    GameLoop loop;
    loop.set_paused(true);
    for (int i = 0; i < 30; ++i) {
        loop.tick_with_dt(1.0f / 60.0f, [](float) {}, [](float) {});
    }
    CHECK(loop.step_count() == 0u);
    CHECK(loop.last_alpha() == doctest::Approx(0.0));
}

TEST_CASE("GameLoop alpha sits in 0..1 between steps") {
    GameLoop loop;
    loop.set_fixed_dt(1.0f / 60.0f);

    // 25 ms of real_dt -> one step + ~8.33 ms remaining in accumulator.
    loop.tick_with_dt(0.025f, [](float) {}, [](float) {});
    CHECK(loop.last_steps() == 1);
    CHECK(loop.last_alpha() >= 0.0f);
    CHECK(loop.last_alpha() < 1.0f);
    CHECK(loop.last_alpha() == doctest::Approx(0.025f / (1.0f / 60.0f) - 1.0f).epsilon(1e-3));
}

TEST_CASE("GameLoop spiral-of-death guard caps steps per frame") {
    GameLoop loop;
    loop.set_fixed_dt(1.0f / 60.0f);
    loop.set_max_steps_per_frame(5);

    // 200 ms of real_dt would otherwise produce 12 steps; capped to 5 plus
    // dropped_steps counter.
    loop.tick_with_dt(0.2f, [](float) {}, [](float) {});
    CHECK(loop.last_steps() == 5);
    CHECK(loop.dropped_steps() > 0u);
}

TEST_CASE("GameLoop clamps real_dt to max_real_dt") {
    GameLoop loop;
    loop.set_max_real_dt(0.05f);
    loop.set_fixed_dt(1.0f / 60.0f);
    loop.set_max_steps_per_frame(100);

    // 1 second of real_dt would produce 60 steps without the clamp; with it,
    // 0.05 s -> ~3 steps. Allow 2 or 3 to absorb float precision on the boundary.
    loop.tick_with_dt(1.0f, [](float) {}, [](float) {});
    CHECK(loop.last_steps() >= 2);
    CHECK(loop.last_steps() <= 3);
}

TEST_CASE("GameLoop replay produces identical sim trace for same inputs") {
    std::vector<float> dts;
    dts.reserve(500);
    // A messy real_dt sequence that mixes accumulation patterns.
    for (unsigned i = 0; i < 500; ++i) {
        const float jitter =
            static_cast<float>((i * 1103515245u + 12345u) % 1000u) / 100000.0f;
        dts.push_back(0.008f + jitter);
    }

    GameLoop a, b;
    const auto trace_a = drive(a, dts);
    const auto trace_b = drive(b, dts);

    // Same starting state + same input sequence + deterministic accumulator
    // math => bit-exact match.
    REQUIRE(trace_a.size() == trace_b.size());
    for (std::size_t i = 0; i < trace_a.size(); ++i) {
        REQUIRE(trace_a[i] == trace_b[i]);
    }
    CHECK(a.step_count() == b.step_count());
    CHECK(a.sim_time()   == b.sim_time());
}
