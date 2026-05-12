# engine/renderer/rhi
#
# Render Hardware Interface - pure C++ headers. Backends (currently only the
# Metal backend in ../metal) provide the .cpp implementations.

add_library(mge_rhi INTERFACE)

target_include_directories(mge_rhi
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(mge_rhi INTERFACE cxx_std_20)

add_library(mge::rhi ALIAS mge_rhi)
