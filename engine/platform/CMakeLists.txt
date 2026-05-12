# engine/platform
#
# Per-OS shim. macOS is the only target in Phase 1. The macos translation
# units use Obj-C++ (.mm) as the AppKit edge, as authorised in ADR-0001.

if(APPLE)
    add_subdirectory(macos)
endif()
