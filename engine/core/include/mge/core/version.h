#pragma once

#include <cstdint>
#include <string_view>

namespace mge::core {

struct Version {
    std::uint16_t major = 0;
    std::uint16_t minor = 0;
    std::uint16_t patch = 0;
};

[[nodiscard]] constexpr Version engine_version() noexcept {
    return Version{0, 0, 1};
}

[[nodiscard]] std::string_view engine_name() noexcept;

[[nodiscard]] std::string_view engine_build_kind() noexcept;

}  // namespace mge::core
