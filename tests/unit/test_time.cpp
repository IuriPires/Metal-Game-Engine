#include "mge/core/time.h"

#include <doctest/doctest.h>

using mge::core::FrameStats;

TEST_CASE("FrameStats reports last sample") {
    FrameStats s;
    CHECK(s.count() == 0);
    s.push(0.016);
    CHECK(s.count() == 1);
    CHECK(s.last_seconds() == doctest::Approx(0.016));
}

TEST_CASE("FrameStats average over small window") {
    FrameStats s;
    s.push(0.010);
    s.push(0.020);
    s.push(0.030);
    CHECK(s.avg_seconds() == doctest::Approx(0.020));
    CHECK(s.min_seconds() == doctest::Approx(0.010));
    CHECK(s.max_seconds() == doctest::Approx(0.030));
}

TEST_CASE("FrameStats wraps at window_size") {
    FrameStats s;
    for (std::size_t i = 0; i < FrameStats::window_size + 5; ++i) {
        s.push(0.001 * static_cast<double>(i));
    }
    CHECK(s.count() == FrameStats::window_size);
    // last 120 samples are 5..124, avg = (5+124)/2 = 64.5 * 0.001
    CHECK(s.avg_seconds() == doctest::Approx(0.0645).epsilon(0.001));
    CHECK(s.max_seconds() == doctest::Approx(0.124));
    CHECK(s.min_seconds() == doctest::Approx(0.005));
}

TEST_CASE("now() is monotonic") {
    auto t0 = mge::core::now();
    auto t1 = mge::core::now();
    CHECK(t1 >= t0);
    CHECK(mge::core::seconds(t1 - t0) >= 0.0);
}
