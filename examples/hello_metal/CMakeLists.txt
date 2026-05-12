# examples/hello_metal
#
# The M1 deliverable: opens a Cocoa window, configures a CAMetalLayer-backed
# view, clears the drawable each frame with an animated color, prints frame
# timing to stdout. Run by executing the binary; close the window to exit.
# By default it auto-exits after a short run so it works in CI smoke tests.

add_executable(hello_metal
    main.cpp
)

target_link_libraries(hello_metal
    PRIVATE
        mge::platform
        mge::renderer_metal
        mge::core
        mge_warnings
        mge_sanitizers
)

target_compile_features(hello_metal PRIVATE cxx_std_20)

set_target_properties(hello_metal PROPERTIES
    MACOSX_BUNDLE              FALSE  # plain executable; bundle comes with the editor
    OUTPUT_NAME                hello_metal
)
