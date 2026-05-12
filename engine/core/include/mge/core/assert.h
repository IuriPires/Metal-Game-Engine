#pragma once

#include <cstdio>
#include <cstdlib>

// MGE_ASSERT(cond, fmt, ...)
//   Debug-only assertion. Compiles away in Release (NDEBUG).
// MGE_VERIFY(cond, fmt, ...)
//   Always evaluated. Stays in Release for invariants that must not be removed.
// MGE_UNREACHABLE(fmt, ...)
//   Mark a code path as unreachable. Aborts in Debug, optimizer hint in Release.
//
// The format string and arguments use fmt-style braces. We keep this file
// dependency-free of fmt (so it's safe to include in any header); the real
// formatting happens via the logger in cpp files that pull in fmt.

#if defined(_MSC_VER)
#define MGE_DEBUGBREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
#define MGE_DEBUGBREAK() __builtin_trap()
#else
#define MGE_DEBUGBREAK() std::abort()
#endif

#if defined(__clang__) || defined(__GNUC__)
#define MGE_UNREACHABLE_INTRINSIC() __builtin_unreachable()
#else
#define MGE_UNREACHABLE_INTRINSIC() ((void)0)
#endif

namespace mge::core::detail {

// Implemented in assert.cpp. Defined as an extern to keep this header free of
// fmt / iostreams. The printf-style attribute lets Clang validate the format
// string at every call site, so non-literal format warnings are not surprises.
#if defined(__clang__) || defined(__GNUC__)
__attribute__((format(printf, 6, 7)))
#endif
void report_failure(const char* kind, const char* expr, const char* file, int line,
                    const char* function, const char* fmt_str, ...) noexcept;

}  // namespace mge::core::detail

#if !defined(NDEBUG)
#define MGE_ASSERT(cond, ...)                                                              \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            ::mge::core::detail::report_failure("ASSERT", #cond, __FILE__, __LINE__,       \
                                                __func__, "" __VA_ARGS__);                 \
            MGE_DEBUGBREAK();                                                              \
        }                                                                                  \
    } while (false)
#else
#define MGE_ASSERT(cond, ...) ((void)0)
#endif

#define MGE_VERIFY(cond, ...)                                                              \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            ::mge::core::detail::report_failure("VERIFY", #cond, __FILE__, __LINE__,       \
                                                __func__, "" __VA_ARGS__);                 \
            MGE_DEBUGBREAK();                                                              \
        }                                                                                  \
    } while (false)

#define MGE_UNREACHABLE(...)                                                               \
    do {                                                                                   \
        ::mge::core::detail::report_failure("UNREACHABLE", "", __FILE__, __LINE__,         \
                                            __func__, "" __VA_ARGS__);                     \
        MGE_DEBUGBREAK();                                                                  \
        MGE_UNREACHABLE_INTRINSIC();                                                       \
    } while (false)
