# engine/assets
#
# M25a: adds glTF + image loaders on top of the existing procedural primitives.
# Loaders sit alongside the procedural mesh helpers so call sites can mix
# baked-in geometry with imported assets.

add_library(mge_assets STATIC
    src/primitives.cpp
    src/pbr_primitives.cpp
    src/texture_load.cpp
    src/gltf_load.cpp
)

target_include_directories(mge_assets
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mge_assets
    PUBLIC
        mge::math
    PRIVATE
        cgltf::cgltf
        stb::stb
        mge_warnings
        mge_sanitizers
)

# stb + cgltf are single-header libs; their IMPLEMENTATION TUs warn under
# our strict baseline. Silence the noisy ones only for this library.
target_compile_options(mge_assets PRIVATE
    -Wno-conversion
    -Wno-sign-conversion
    -Wno-double-promotion
    -Wno-old-style-cast
    -Wno-cast-align
    -Wno-shadow
    -Wno-implicit-fallthrough
)

target_compile_features(mge_assets PUBLIC cxx_std_20)

add_library(mge::assets ALIAS mge_assets)
