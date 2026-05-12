# engine/renderer/metal
#
# Metal backend implementing the mge::rhi interface. One TU per RHI type.
# metal_cpp_impl.cpp is the sole TU defining the *_PRIVATE_IMPLEMENTATION
# macros.

add_library(mge_rhi_metal STATIC
    src/metal_cpp_impl.cpp
    src/device.cpp
    src/queue.cpp
    src/command_buffer.cpp
    src/encoder.cpp
    src/buffer.cpp
    src/texture.cpp
    src/sampler.cpp
    src/shader.cpp
    src/pipeline.cpp
    src/swapchain.cpp
    src/offscreen_readback.cpp
    src/acceleration_structure.cpp
)

target_include_directories(mge_rhi_metal
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(mge_rhi_metal PUBLIC cxx_std_20)

target_link_libraries(mge_rhi_metal
    PUBLIC
        mge::rhi
        metal_cpp::metal_cpp
    PRIVATE
        mge::core
        mge_warnings
        mge_sanitizers
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    target_compile_options(mge_rhi_metal PRIVATE
        -Wno-deprecated-declarations
        -Wno-old-style-cast
    )
endif()

add_library(mge::rhi_metal ALIAS mge_rhi_metal)
