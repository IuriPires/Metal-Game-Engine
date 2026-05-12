# engine/memory
#
# Header-only allocators. Stats hooks feed the overlay and Tracy when those
# land.

add_library(mge_memory INTERFACE)

target_include_directories(mge_memory
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(mge_memory INTERFACE cxx_std_20)

add_library(mge::memory ALIAS mge_memory)
