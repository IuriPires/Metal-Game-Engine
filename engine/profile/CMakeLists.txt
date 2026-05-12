# engine/profile
#
# CPU zone profiler with RAII ScopeTimer and rolling-window stats per zone
# name. GPU timestamp integration (Metal counter sample buffers) is deferred
# to M11.b. Thread-safe for the Phase 1.5+ multi-thread scheduler.

add_library(mge_profile STATIC
    src/profiler.cpp
)

target_include_directories(mge_profile
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mge_profile
    PUBLIC
        mge::core
    PRIVATE
        mge_warnings
        mge_sanitizers
)

target_compile_features(mge_profile PUBLIC cxx_std_20)

add_library(mge::profile ALIAS mge_profile)
