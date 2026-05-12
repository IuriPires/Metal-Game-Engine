cmake_minimum_required(VERSION 3.27)

# Third-party libs pinned at versions that predate CMake 4.0 may still declare
# `cmake_minimum_required(VERSION 2.x)`. CMake 4 removes pre-3.5 compatibility,
# so we set the policy floor for fetched subprojects. Drop when all deps are
# updated.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum policy version for third_party CMake projects" FORCE)

project(MetalGameEngine
    VERSION 0.0.1
    DESCRIPTION "Modern AAA-style game engine in C++20 targeting Metal"
    LANGUAGES CXX C
)

# -----------------------------------------------------------------------------
# Global config
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Apple Silicon focus
if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architectures" FORCE)
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS deployment target" FORCE)
    endif()
endif()

# -----------------------------------------------------------------------------
# Options
# -----------------------------------------------------------------------------
option(MGE_BUILD_TESTS    "Build unit/integration tests"   ON)
option(MGE_BUILD_EXAMPLES "Build example demos"            ON)
option(MGE_BUILD_TOOLS    "Build offline tools"            ON)
option(MGE_ENABLE_TRACY   "Enable Tracy profiler hooks"    ON)
option(MGE_ENABLE_ASAN    "Enable AddressSanitizer"        OFF)
option(MGE_ENABLE_TSAN    "Enable ThreadSanitizer"         OFF)
option(MGE_ENABLE_UBSAN   "Enable UndefinedBehaviorSanitizer" OFF)
option(MGE_WERROR         "Treat warnings as errors"       OFF)

include(cmake/Warnings.cmake)
include(cmake/Sanitizers.cmake)

# -----------------------------------------------------------------------------
# Third-party
# -----------------------------------------------------------------------------
add_subdirectory(third_party)

# -----------------------------------------------------------------------------
# Engine
# -----------------------------------------------------------------------------
add_subdirectory(engine)

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
if(MGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# -----------------------------------------------------------------------------
# Examples
# -----------------------------------------------------------------------------
if(MGE_BUILD_EXAMPLES AND APPLE)
    add_subdirectory(examples)
endif()

# Tools - Phase 1.5+
# if(MGE_BUILD_TOOLS)
#     add_subdirectory(tools)
# endif()

message(STATUS "MetalGameEngine ${PROJECT_VERSION} configured")
message(STATUS "  Build type:    ${CMAKE_BUILD_TYPE}")
message(STATUS "  Architectures: ${CMAKE_OSX_ARCHITECTURES}")
message(STATUS "  Deployment:    ${CMAKE_OSX_DEPLOYMENT_TARGET}")
message(STATUS "  Tests:         ${MGE_BUILD_TESTS}")
message(STATUS "  Tracy:         ${MGE_ENABLE_TRACY}")
