#include "mge/profile/profiler.h"

#include <doctest/doctest.h>

#include <thread>

using namespace mge::profile;

TEST_CASE("Profiler records a zone duration") {
    Profiler::get().reset();
    {
        ScopeTimer t("test.short");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto stats = Profiler::get().snapshot();
    REQUIRE(stats.size() == 1);
    CHECK(stats[0].name == "test.short");
    CHECK(stats[0].samples == 1);
    CHECK(stats[0].last_ms >= 0.5);  // at least ~1ms; allow scheduler jitter
}

TEST_CASE("Profiler aggregates min/max/avg across samples") {
    Profiler::get().reset();
    for (int i = 0; i < 5; ++i) {
        ScopeTimer t("loop");
        std::this_thread::sleep_for(std::chrono::microseconds(200 + i * 50));
    }
    const auto stats = Profiler::get().snapshot();
    REQUIRE(stats.size() == 1);
    const auto& s = stats[0];
    CHECK(s.samples == 5);
    CHECK(s.min_ms <= s.avg_ms);
    CHECK(s.avg_ms <= s.max_ms);
    CHECK(s.min_ms > 0.0);
}

TEST_CASE("Profiler keeps zones independent by name") {
    Profiler::get().reset();
    { ScopeTimer a("alpha"); std::this_thread::sleep_for(std::chrono::microseconds(100)); }
    { ScopeTimer b("beta");  std::this_thread::sleep_for(std::chrono::microseconds(200)); }
    { ScopeTimer a("alpha"); std::this_thread::sleep_for(std::chrono::microseconds(100)); }

    const auto stats = Profiler::get().snapshot();
    REQUIRE(stats.size() == 2);
    std::size_t a_idx = stats[0].name == "alpha" ? 0 : 1;
    std::size_t b_idx = 1 - a_idx;
    CHECK(stats[a_idx].name == "alpha");
    CHECK(stats[a_idx].samples == 2);
    CHECK(stats[b_idx].name == "beta");
    CHECK(stats[b_idx].samples == 1);
}

TEST_CASE("Profiler rolling window caps at window_size") {
    Profiler::get().reset();
    for (std::size_t i = 0; i < Profiler::window_size + 30; ++i) {
        ScopeTimer t("rolling");
    }
    const auto stats = Profiler::get().snapshot();
    REQUIRE(stats.size() == 1);
    CHECK(stats[0].samples == Profiler::window_size);
}

TEST_CASE("Profiler MGE_PROFILE_ZONE macro records a sample") {
    Profiler::get().reset();
    {
        MGE_PROFILE_ZONE("macro.zone");
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    const auto stats = Profiler::get().snapshot();
    REQUIRE(stats.size() == 1);
    CHECK(stats[0].name == "macro.zone");
    CHECK(stats[0].samples == 1);
}
