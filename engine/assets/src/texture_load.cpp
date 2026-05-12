#include "mge/assets/texture_load.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO  // we read files ourselves
#include "stb_image.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace mge::assets {

DecodedImage decode_image(std::span<const std::uint8_t> bytes) noexcept {
    DecodedImage out;
    int w = 0, h = 0, channels_in_file = 0;
    // Always request 4 channels — uniform RGBA8 across the engine simplifies
    // texture creation + upload (one `bytes_per_row` formula).
    stbi_uc* px = stbi_load_from_memory(
        bytes.data(), static_cast<int>(bytes.size()),
        &w, &h, &channels_in_file, 4);
    if (px == nullptr || w <= 0 || h <= 0) {
        return out;
    }
    out.width  = static_cast<std::uint32_t>(w);
    out.height = static_cast<std::uint32_t>(h);
    out.channels = 4;
    out.pixels.reset(new std::uint8_t[static_cast<std::size_t>(w) * h * 4]);
    std::memcpy(out.pixels.get(), px,
                 static_cast<std::size_t>(w) * h * 4);
    stbi_image_free(px);
    return out;
}

DecodedImage load_image_from_file(std::string_view path) noexcept {
    DecodedImage out;
    std::ifstream f(std::string{path}, std::ios::binary | std::ios::ate);
    if (!f) {
        std::fprintf(stderr, "[assets] image not found: %.*s\n",
                     static_cast<int>(path.size()), path.data());
        return out;
    }
    const auto size = static_cast<std::size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(size);
    if (!f.read(reinterpret_cast<char*>(buf.data()),
                 static_cast<std::streamsize>(size))) {
        return out;
    }
    return decode_image(std::span<const std::uint8_t>(buf.data(), buf.size()));
}

}  // namespace mge::assets
