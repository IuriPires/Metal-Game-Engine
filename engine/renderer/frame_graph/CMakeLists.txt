# engine/renderer/frame_graph
#
# v1 (M5): single graphics queue, lifetime-based aliasing of transients,
# topological scheduling, Graphviz dump. See ADR-0005 and
# docs/RENDERING_PIPELINE.md.

add_library(mge_frame_graph STATIC
    src/frame_graph.cpp
)

target_include_directories(mge_frame_graph
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mge_frame_graph
    PUBLIC
        mge::rhi
        mge::core
    PRIVATE
        mge_warnings
        mge_sanitizers
)

target_compile_features(mge_frame_graph PUBLIC cxx_std_20)

add_library(mge::frame_graph ALIAS mge_frame_graph)
