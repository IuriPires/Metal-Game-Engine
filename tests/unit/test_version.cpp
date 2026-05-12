#include "mge/core/version.h"

#include <doctest/doctest.h>

TEST_CASE("engine name is MetalGameEngine") {
    CHECK(mge::core::engine_name() == "MetalGameEngine");
}

TEST_CASE("engine version is sane") {
    constexpr auto v = mge::core::engine_version();
    CHECK(v.major == 0);
    CHECK(v.minor == 0);
    CHECK(v.patch == 1);
}

TEST_CASE("build kind reflects compile mode") {
#if defined(NDEBUG)
    CHECK(mge::core::engine_build_kind() == "release");
#else
    CHECK(mge::core::engine_build_kind() == "debug");
#endif
}
