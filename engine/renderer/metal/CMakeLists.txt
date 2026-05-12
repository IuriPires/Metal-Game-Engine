# engine/renderer/metal
#
# Phase 1 / M1: minimal Metal-cpp glue - device wrapper, swapchain helper,
# clear-only renderer, offscreen target with readback. M3 will fold these
# behind the RHI.

add_library(mge_renderer_metal STATIC
    src/metal_cpp_impl.cpp
    src/device.cpp
    src/swapchain.cpp
    src/clear_renderer.cpp
    src/offscreen.cpp
)

target_include_directories(mge_renderer_metal
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(mge_renderer_metal PUBLIC cxx_std_20)

target_link_libraries(mge_renderer_metal
    PUBLIC
        metal_cpp::metal_cpp
    PRIVATE
        mge::core
        mge_warnings
        mge_sanitizers
)

# metal-cpp triggers a few conversion warnings on Apple Clang that we should
# not let our strict warning baseline reject. Silence them locally.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    target_compile_options(mge_renderer_metal PRIVATE
        -Wno-deprecated-declarations
        -Wno-old-style-cast
    )
endif()

add_library(mge::renderer_metal ALIAS mge_renderer_metal)
