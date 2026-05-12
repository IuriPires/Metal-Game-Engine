# engine/platform/macos
#
# Tiny .mm shim around NSApplication + NSWindow + CAMetalLayer. The rest of
# the engine sees a pure C++ API.

add_library(mge_platform STATIC
    src/app.mm
    src/window.mm
)

target_include_directories(mge_platform
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_compile_features(mge_platform PUBLIC cxx_std_20)

# Obj-C++ specific flags - no ARC (we manage retain/release explicitly to keep
# the shim predictable), Obj-C exceptions disabled.
set_source_files_properties(src/app.mm src/window.mm
    PROPERTIES COMPILE_FLAGS "-fno-objc-arc")

target_link_libraries(mge_platform
    PUBLIC
        metal_cpp::metal_cpp
    PRIVATE
        mge::core
        mge::renderer_metal
        mge_warnings
        mge_sanitizers
        "-framework AppKit"
        "-framework Cocoa"
)

# AppKit triggers a few warnings (deprecated APIs, old-style casts in headers)
# at our strict baseline. Silence locally.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    target_compile_options(mge_platform PRIVATE
        -Wno-deprecated-declarations
        -Wno-old-style-cast
        -Wno-cast-align
    )
endif()

add_library(mge::platform ALIAS mge_platform)
