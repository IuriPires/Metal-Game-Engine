#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace mge::assets {

// Decoded image, owned by the loader. Layout is always 8-bit-per-channel
// RGBA — the loader expands grayscale / RGB inputs to RGBA so the engine
// only has to handle one upload format. `pixels` is freed when the object
// drops.
struct DecodedImage {
    std::uint32_t width  = 0;
    std::uint32_t height = 0;
    std::uint32_t channels = 4;            // always 4 (RGBA8) on success
    std::unique_ptr<std::uint8_t[]> pixels;

    [[nodiscard]] std::size_t byte_size() const noexcept {
        return static_cast<std::size_t>(width) * height * channels;
    }
    [[nodiscard]] std::size_t bytes_per_row() const noexcept {
        return static_cast<std::size_t>(width) * channels;
    }
    [[nodiscard]] bool valid() const noexcept {
        return pixels && width > 0 && height > 0;
    }
};

// Decode PNG / JPG / BMP / TGA from an in-memory buffer. Returns an empty
// DecodedImage on failure; check `.valid()` before using.
[[nodiscard]] DecodedImage decode_image(std::span<const std::uint8_t> bytes) noexcept;

// Convenience: decode from a filesystem path.
[[nodiscard]] DecodedImage load_image_from_file(std::string_view path) noexcept;

}  // namespace mge::assets
