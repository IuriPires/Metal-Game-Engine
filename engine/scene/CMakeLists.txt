# engine/scene
#
# Owns Camera + camera controllers + InputState. Header-only until M26a;
# now a STATIC lib so `camera_controller.cpp` has a home.

add_library(mge_scene STATIC
    src/camera_controller.cpp
)

target_include_directories(mge_scene
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mge_scene
    PUBLIC mge::math
    PRIVATE mge_warnings mge_sanitizers
)
target_compile_features(mge_scene PUBLIC cxx_std_20)

add_library(mge::scene ALIAS mge_scene)
