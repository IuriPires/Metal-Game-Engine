# third_party
#
# Strategy: declare all dependencies up front via FetchContent, but only
# MakeAvailable the ones a milestone has already consumed. This keeps the
# initial M0 build fast and ensures each dep is justified by a milestone.
#
# Policy:
#  - All deps pinned by commit/tag + SHA256 (or git tag for now; SHAs hardened later).
#  - Each new dep needs an ADR entry.
#  - No dep larger than ~5MB in source without explicit reason.

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)
set(FETCHCONTENT_UPDATES_DISCONNECTED TRUE)

# -----------------------------------------------------------------------------
# doctest — unit test framework (header-only)
#   Consumed by: M0+ tests
# -----------------------------------------------------------------------------
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(doctest)

# -----------------------------------------------------------------------------
# fmt — string formatting (until Apple Clang ships <format> uniformly)
#   Consumed by: M2 (logger)
# -----------------------------------------------------------------------------
FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        10.2.1
    GIT_SHALLOW    TRUE
)
# FetchContent_MakeAvailable(fmt)  # enable at M2

# -----------------------------------------------------------------------------
# metal-cpp — Apple's C++ binding for Metal
#   Consumed by: M1 (first Metal frame)
#
# Note: metal-cpp is distributed as a ZIP from developer.apple.com without
# a public git repo. We mirror via the Apple download URL.
# -----------------------------------------------------------------------------
FetchContent_Declare(metal_cpp
    URL          https://developer.apple.com/metal/cpp/files/metal-cpp_26.4.zip
    URL_HASH     SHA256=4e7587c0f351d782f1f3e7c67c649b40d0ba780c051b2f6729f00ddc61ada28c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(metal_cpp)

# metal-cpp ships as headers only; package them as an INTERFACE target with
# the right include path and Apple framework links. The single .cpp file that
# defines MTL_PRIVATE_IMPLEMENTATION etc. lives in engine/renderer/metal/.
if(APPLE AND TARGET metal_cpp::metal_cpp)
    message(WARNING "metal_cpp::metal_cpp target already exists")
elseif(APPLE)
    add_library(metal_cpp_headers INTERFACE)
    # FetchContent's URL extraction strips the archive's top-level `metal-cpp/`
    # directory; the headers end up directly under `${metal_cpp_SOURCE_DIR}/`
    # (Foundation/, Metal/, QuartzCore/, MetalFX/, SingleHeader/).
    # SYSTEM keyword silences warnings from metal-cpp headers under our strict
    # warning baseline (anonymous-struct, sign-conversion, etc.).
    target_include_directories(metal_cpp_headers SYSTEM INTERFACE
        ${metal_cpp_SOURCE_DIR}
    )
    target_link_libraries(metal_cpp_headers INTERFACE
        "-framework Foundation"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework CoreGraphics"
    )
    target_compile_features(metal_cpp_headers INTERFACE cxx_std_17)
    add_library(metal_cpp::metal_cpp ALIAS metal_cpp_headers)
endif()

# -----------------------------------------------------------------------------
# cgltf — single-header glTF loader
#   Consumed by: M4 (mesh loading)
# -----------------------------------------------------------------------------
FetchContent_Declare(cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG        v1.14
    GIT_SHALLOW    TRUE
)
# FetchContent_MakeAvailable(cgltf)  # enable at M4

# -----------------------------------------------------------------------------
# stb — image loading + writing (textures + golden PNGs)
#   Consumed by: M3 (golden image), M4 (textures)
# -----------------------------------------------------------------------------
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master  # TODO: pin at M3
    GIT_SHALLOW    TRUE
)
# FetchContent_MakeAvailable(stb)  # enable at M3

# -----------------------------------------------------------------------------
# Tracy — frame profiler
#   Consumed by: M11 (profiling overlay), but used opportunistically from M2+
# -----------------------------------------------------------------------------
FetchContent_Declare(tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG        v0.11.0
    GIT_SHALLOW    TRUE
)
# FetchContent_MakeAvailable(tracy)  # enable at M2 once Tracy::Client target exists

# -----------------------------------------------------------------------------
# Aggregate interface for tests
# -----------------------------------------------------------------------------
add_library(mge_thirdparty INTERFACE)
target_link_libraries(mge_thirdparty INTERFACE doctest::doctest)
