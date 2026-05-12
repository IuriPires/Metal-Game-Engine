#pragma once

#include "mge/rhi/enums.h"

#include <cstdint>
#include <string>

namespace mge::rhi {

struct TextureDesc {
    std::uint32_t width      = 0;
    std::uint32_t height     = 0;
    std::uint32_t depth      = 1;
    std::uint32_t mip_levels = 1;
    PixelFormat   format     = PixelFormat::RGBA8Unorm;
    TextureUsage  usage      = TextureUsage::ShaderRead;
    StorageMode   storage    = StorageMode::Private;
    std::string   label;
};

class Texture {
public:
    ~Texture();
    Texture(const Texture&)            = delete;
    Texture& operator=(const Texture&) = delete;

    Texture(Texture&& other) noexcept
        : native_(other.native_), desc_(std::move(other.desc_)), owned_(other.owned_) {
        other.native_ = nullptr;
        other.owned_  = false;
    }
    Texture& operator=(Texture&&) = delete;

    [[nodiscard]] std::uint32_t width() const noexcept { return desc_.width; }
    [[nodiscard]] std::uint32_t height() const noexcept { return desc_.height; }
    [[nodiscard]] PixelFormat   format() const noexcept { return desc_.format; }
    [[nodiscard]] TextureUsage  usage() const noexcept { return desc_.usage; }
    [[nodiscard]] const std::string& label() const noexcept { return desc_.label; }

    [[nodiscard]] void*       native() noexcept { return native_; }
    [[nodiscard]] const void* native() const noexcept { return native_; }

    // M25a — upload CPU-side pixel data into a mip + sub-region. The texture
    // must be `StorageMode::Shared` (Private textures require a blit through
    // a staging buffer — M25a.b will add that path). `bytes_per_row` matches
    // Metal's expected source pitch (width * bytes_per_pixel for tightly
    // packed data). Returns false on invalid region / wrong storage mode.
    bool upload_region(std::uint32_t mip_level,
                       std::uint32_t x, std::uint32_t y,
                       std::uint32_t width, std::uint32_t height,
                       const void* bytes, std::size_t bytes_per_row) noexcept;

private:
    friend class Device;
    friend class Swapchain;
    friend class SwapchainFrame;
    Texture(void* native, TextureDesc desc, bool owned = true) noexcept
        : native_(native), desc_(std::move(desc)), owned_(owned) {}

    void*       native_ = nullptr;
    TextureDesc desc_;
    bool        owned_ = true;  // false for swapchain-acquired textures
};

}  // namespace mge::rhi
