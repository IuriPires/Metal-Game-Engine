#pragma once

// Architecture detection. Defines MGE_ARCH_NEON when AArch64 NEON is available
// (i.e. Apple Silicon and other ARMv8+ platforms). Code that wants to opt into
// intrinsics gates on this; the scalar baseline must remain functional and
// is what the unit tests cross-check against.

#if defined(__ARM_NEON) && (defined(__aarch64__) || defined(_M_ARM64))
#define MGE_ARCH_NEON 1
#include <arm_neon.h>
#else
#define MGE_ARCH_NEON 0
#endif

#if defined(__APPLE__)
#define MGE_APPLE 1
#else
#define MGE_APPLE 0
#endif
