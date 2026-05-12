#include "mge/rhi/texture.h"

#include "mge/renderer/metal/metal_cpp.h"

namespace mge::rhi {

Texture::~Texture() {
    if (owned_ && native_ != nullptr) {
        static_cast<MTL::Texture*>(native_)->release();
    }
}

bool Texture::upload_region(std::uint32_t mip_level,
                              std::uint32_t x, std::uint32_t y,
                              std::uint32_t width, std::uint32_t height,
                              const void* bytes, std::size_t bytes_per_row) noexcept {
    if (native_ == nullptr || bytes == nullptr) return false;
    if (desc_.storage != StorageMode::Shared &&
        desc_.storage != StorageMode::Managed) {
        // Private textures need a staging buffer + blit encoder; that path
        // arrives with M25b. For now, callers must declare Shared.
        return false;
    }
    if (x + width  > desc_.width  ||
        y + height > desc_.height ||
        mip_level >= desc_.mip_levels) {
        return false;
    }
    auto* tex = static_cast<MTL::Texture*>(native_);
    tex->replaceRegion(MTL::Region::Make2D(x, y, width, height),
                        mip_level, bytes, bytes_per_row);
    return true;
}

}  // namespace mge::rhi
