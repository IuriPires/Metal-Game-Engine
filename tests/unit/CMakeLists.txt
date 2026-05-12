# tests/unit
#
# Fast doctest-based unit tests. One executable for the whole suite for now;
# split per subsystem if compile time becomes a problem.

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)

add_executable(mge_unit_tests
    test_smoke.cpp
    test_version.cpp
    test_time.cpp
    test_result.cpp
    test_log.cpp
    test_math.cpp
    test_memory.cpp
    test_camera.cpp
    test_primitives.cpp
    test_pbr_primitives.cpp
    test_frustum.cpp
    test_game_loop.cpp
)

target_link_libraries(mge_unit_tests
    PRIVATE
        mge::core
        mge::math
        mge::memory
        mge::scene
        mge::assets
        doctest::doctest
        mge_warnings
        mge_sanitizers
)

target_compile_features(mge_unit_tests PRIVATE cxx_std_20)

doctest_discover_tests(mge_unit_tests
    ADD_LABELS 1
)
