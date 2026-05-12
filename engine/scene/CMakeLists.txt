# engine/scene
#
# Header-only for now. Owns Camera; future homes for Transform, Light, etc.

add_library(mge_scene INTERFACE)

target_include_directories(mge_scene
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mge_scene INTERFACE mge::math)
target_compile_features(mge_scene INTERFACE cxx_std_20)

add_library(mge::scene ALIAS mge_scene)
