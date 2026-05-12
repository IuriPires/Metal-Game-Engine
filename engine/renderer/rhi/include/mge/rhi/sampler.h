#pragma once

#include <cstdint>
#include <string>

namespace mge::rhi {

enum class FilterMode : std::uint8_t {
    Nearest = 0,
    Linear,
};

enum class MipFilter : std::uint8_t {
    NotMipmapped = 0,
    Nearest,
    Linear,
};

enum class AddressMode : std::uint8_t {
    Repeat = 0,
    MirrorRepeat,
    ClampToEdge,
    ClampToZero,
};

struct SamplerDesc {
    FilterMode  min_filter = FilterMode::Linear;
    FilterMode  mag_filter = FilterMode::Linear;
    MipFilter   mip_filter = MipFilter::NotMipmapped;
    AddressMode address_u  = AddressMode::ClampToEdge;
    AddressMode address_v  = AddressMode::ClampToEdge;
    AddressMode address_w  = AddressMode::ClampToEdge;
    std::uint8_t max_anisotropy = 1;
    std::string  label;
};

class Sampler {
public:
    ~Sampler();
    Sampler(const Sampler&)            = delete;
    Sampler& operator=(const Sampler&) = delete;

    [[nodiscard]] void*       native() noexcept { return native_; }
    [[nodiscard]] const void* native() const noexcept { return native_; }
    [[nodiscard]] const std::string& label() const noexcept { return label_; }

private:
    friend class Device;
    Sampler(void* native, std::string label) noexcept
        : native_(native), label_(std::move(label)) {}

    void*       native_ = nullptr;
    std::string label_;
};

}  // namespace mge::rhi
