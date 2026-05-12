# engine/core
#
# Foundational utilities. Phase 1 will grow this into: Logger, Assert,
# Result, Time, Config. M0 ships only the version stub so the build
# pipeline (CMake + Ninja + doctest) can be smoke-tested end to end.

add_library(mge_core STATIC
    src/version.cpp
    src/time.cpp
)

target_include_directories(mge_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(mge_core PUBLIC cxx_std_20)

target_link_libraries(mge_core
    PRIVATE
        mge_warnings
        mge_sanitizers
)

add_library(mge::core ALIAS mge_core)
