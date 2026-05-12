#pragma once

#include "mge/rhi/enums.h"

#include <string>
#include <string_view>

namespace mge::rhi {

struct ShaderSourceDesc {
    std::string_view msl_source;     // Metal Shading Language, UTF-8.
    std::string      label;
};

// Holds a compiled shader library (Metal: MTLLibrary). One library can host
// multiple entry points; the function name is supplied at pipeline creation.
class Shader {
public:
    ~Shader();
    Shader(const Shader&)            = delete;
    Shader& operator=(const Shader&) = delete;

    [[nodiscard]] const std::string& label() const noexcept { return label_; }

    [[nodiscard]] void*       native() noexcept { return native_; }
    [[nodiscard]] const void* native() const noexcept { return native_; }

private:
    friend class Device;
    Shader(void* native, std::string label) noexcept
        : native_(native), label_(std::move(label)) {}

    void*       native_ = nullptr;
    std::string label_;
};

}  // namespace mge::rhi
