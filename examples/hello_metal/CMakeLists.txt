# examples/hello_metal
#
# M3 deliverable: opens a Cocoa window, builds an RHI Device + Queue +
# Swapchain + RenderPipeline from inline MSL, draws an interpolated-color
# triangle each frame. --headless smoke-tests the device path in CI.

add_executable(hello_metal
    main.cpp
)

target_link_libraries(hello_metal
    PRIVATE
        mge::platform
        mge::rhi_metal
        mge::core
        mge_warnings
        mge_sanitizers
)

target_compile_features(hello_metal PRIVATE cxx_std_20)

set_target_properties(hello_metal PROPERTIES
    MACOSX_BUNDLE              FALSE
    OUTPUT_NAME                hello_metal
)
