# tests/integration
#
# End-to-end tests that exercise a real Metal device through the RHI.
# CI runs them on macos-14 arm64.

if(NOT APPLE)
    return()
endif()

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)

add_executable(mge_integration_tests
    test_clear_rhi.cpp
    test_triangle_offscreen.cpp
)

target_link_libraries(mge_integration_tests
    PRIVATE
        mge::rhi_metal
        mge::core
        doctest::doctest
        mge_warnings
        mge_sanitizers
)

target_compile_features(mge_integration_tests PRIVATE cxx_std_20)

doctest_discover_tests(mge_integration_tests
    ADD_LABELS 1
    PROPERTIES
        LABELS "integration"
)
