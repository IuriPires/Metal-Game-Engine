# engine/assets
#
# Phase 1: procedural primitives only. cgltf and stb_image loaders come at M5.

add_library(mge_assets STATIC
    src/primitives.cpp
)

target_include_directories(mge_assets
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mge_assets
    PUBLIC
        mge::math
    PRIVATE
        mge_warnings
        mge_sanitizers
)

target_compile_features(mge_assets PUBLIC cxx_std_20)

add_library(mge::assets ALIAS mge_assets)
