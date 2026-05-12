# engine/math
#
# Header-only math: vec/mat/quat/aabb with SoA-friendly layouts and NEON
# intrinsics on Apple Silicon. Layout is ABI-compatible with MSL packed types
# (asserted at compile time).

add_library(mge_math INTERFACE)

target_include_directories(mge_math
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(mge_math INTERFACE cxx_std_20)

add_library(mge::math ALIAS mge_math)
