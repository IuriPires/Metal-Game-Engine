#include "mge/core/version.h"

namespace mge::core {

std::string_view engine_name() noexcept {
    return "MetalGameEngine";
}

std::string_view engine_build_kind() noexcept {
#if defined(NDEBUG)
    return "release";
#else
    return "debug";
#endif
}

}  // namespace mge::core
