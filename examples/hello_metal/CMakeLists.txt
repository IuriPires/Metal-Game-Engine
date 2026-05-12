# examples/hello_metal
#
# M4 deliverable: a Lambert-lit rotating cube. Uses RHI + scene::Camera +
# assets::make_cube + inline MSL shader. Resize-aware (depth texture
# reallocates, camera aspect updates).

add_executable(hello_metal
    main.cpp
)

target_link_libraries(hello_metal
    PRIVATE
        mge::platform
        mge::rhi_metal
        mge::scene
        mge::assets
        mge::math
        mge::core
        mge_warnings
        mge_sanitizers
)

target_compile_features(hello_metal PRIVATE cxx_std_20)

set_target_properties(hello_metal PROPERTIES
    MACOSX_BUNDLE              FALSE
    OUTPUT_NAME                hello_metal
)
