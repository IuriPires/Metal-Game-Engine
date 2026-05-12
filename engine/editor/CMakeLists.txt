# engine/editor
#
# M18 — Dear ImGui-based editor chrome. The editor is a regular FrameGraph
# pass that draws after `overlay`, before the final present. Backend is the
# upstream Metal + OSX impl from ImGui at the platform edge (.mm files;
# precedent in ADR-0001).

if(NOT APPLE)
    return()
endif()

add_library(mge_editor STATIC
    src/editor.cpp
    src/theme.cpp
    src/chrome.cpp
    src/imgui_platform.mm
)

target_include_directories(mge_editor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(mge_editor PUBLIC cxx_std_20)

target_link_libraries(mge_editor
    PUBLIC
        imgui::imgui
        mge::rhi
        mge::core
    PRIVATE
        mge::platform
        mge::rhi_metal
        metal_cpp::metal_cpp
        mge_warnings
        mge_sanitizers
)

# Editor TUs use Apple headers, so we soften a couple of warning flags that
# fight Apple/ImGui code; the engine baseline stays strict elsewhere.
target_compile_options(mge_editor PRIVATE
    -Wno-deprecated-declarations
    -Wno-old-style-cast
    -Wno-conversion
)

add_library(mge::editor ALIAS mge_editor)
