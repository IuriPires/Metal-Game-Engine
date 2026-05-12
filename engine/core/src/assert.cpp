#include "mge/core/assert.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace mge::core::detail {

void report_failure(const char* kind, const char* expr, const char* file, int line,
                    const char* function, const char* fmt_str, ...) noexcept {
    std::fprintf(stderr, "\n[MGE %s] %s:%d  in %s\n", kind, file, line, function);
    if (expr != nullptr && expr[0] != '\0') {
        std::fprintf(stderr, "    condition:  %s\n", expr);
    }
    if (fmt_str != nullptr && fmt_str[0] != '\0') {
        std::fprintf(stderr, "    message:    ");
        std::va_list args;
        va_start(args, fmt_str);
        // The function attribute on the declaration validates format strings at
        // call sites; the cast acknowledges that we then forward a runtime
        // string to vfprintf where Clang otherwise insists on a literal.
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
        std::vfprintf(stderr, fmt_str, args);
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        va_end(args);
        std::fputc('\n', stderr);
    }
    std::fflush(stderr);
}

}  // namespace mge::core::detail
