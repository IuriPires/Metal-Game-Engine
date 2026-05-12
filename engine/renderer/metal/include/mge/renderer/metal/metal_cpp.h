#pragma once

// Centralized metal-cpp include. All engine code should `#include
// "mge/renderer/metal/metal_cpp.h"` instead of poking metal-cpp headers
// directly. The one .cpp file that defines the *_PRIVATE_IMPLEMENTATION
// macros lives at engine/renderer/metal/src/metal_cpp_impl.cpp.

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
