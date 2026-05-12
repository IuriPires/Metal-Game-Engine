# examples/hello_metal
#
# M5 deliverable: the same Lambert cube as M4, driven through a FrameGraph.
# Imports the swapchain backbuffer, declares a transient depth texture,
# emits a "cube" pass. Graph is rebuilt every frame (cheap, resize-safe).

add_executable(hello_metal
    main.cpp
)

target_link_libraries(hello_metal
    PRIVATE
        mge::platform
        mge::rhi_metal
        mge::frame_graph
        mge::scene
        mge::assets
        mge::math
        mge::core
        mge::profile
        mge_warnings
        mge_sanitizers
)

target_compile_features(hello_metal PRIVATE cxx_std_20)

set_target_properties(hello_metal PROPERTIES
    MACOSX_BUNDLE              FALSE
    OUTPUT_NAME                hello_metal
)
