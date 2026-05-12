#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

// First test of the project. Proves the build pipeline (CMake + Ninja + doctest)
// is wired end-to-end. Real tests grow from M2 onward.

TEST_CASE("smoke: doctest is wired") {
    CHECK(1 + 1 == 2);
}

TEST_CASE("smoke: floating-point math is sane") {
    constexpr double pi_approx = 3.14159265358979323846;
    CHECK(pi_approx > 3.0);
    CHECK(pi_approx < 4.0);
}
