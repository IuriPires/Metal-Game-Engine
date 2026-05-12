// Offscreen clear test: drive a Metal device through a clear-only render
// pass into a private texture, blit to a shared buffer, and verify the
// pixel bytes. Runs without a window. This is the most basic proof that
// the Metal device, command queue, render encoder, and blit encoder all
// work end-to-end.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "mge/renderer/metal/clear_color.h"
#include "mge/renderer/metal/device.h"
#include "mge/renderer/metal/offscreen.h"

#include <cmath>
#include <memory>

using mge::renderer::metal::ClearColor;
using mge::renderer::metal::Device;
using mge::renderer::metal::Offscreen;

namespace {

// Clear in linear RGBA8Unorm space. The hardware quantizes each channel to
// 8 bits via `(value * 255 + 0.5)`. Allow ±1 unit of tolerance.
constexpr int tol = 1;

bool near_equal(int a, int b) {
    return std::abs(a - b) <= tol;
}

int linear_to_u8(double v) {
    if (v <= 0.0) return 0;
    if (v >= 1.0) return 255;
    return static_cast<int>(v * 255.0 + 0.5);
}

}  // namespace

TEST_CASE("Metal device can be created") {
    std::unique_ptr<Device> dev{Device::create()};
    REQUIRE(dev != nullptr);
    REQUIRE_FALSE(dev->name().empty());
    INFO("device: ", dev->name());
    INFO("unified memory: ", dev->has_unified_memory());
    INFO("low power: ", dev->is_low_power());
    INFO("ray tracing: ", dev->supports_ray_tracing());
}

TEST_CASE("offscreen clear writes the requested color") {
    std::unique_ptr<Device> dev{Device::create()};
    REQUIRE(dev != nullptr);

    constexpr unsigned w = 16;
    constexpr unsigned h = 16;
    Offscreen           target(*dev, w, h);

    SUBCASE("pure red") {
        const ClearColor c{1.0, 0.0, 0.0, 1.0};
        const auto       px = target.clear_and_read_pixel(c, 4, 7);
        CHECK(near_equal(px[0], linear_to_u8(c.r)));
        CHECK(near_equal(px[1], linear_to_u8(c.g)));
        CHECK(near_equal(px[2], linear_to_u8(c.b)));
        CHECK(near_equal(px[3], linear_to_u8(c.a)));
    }

    SUBCASE("teal") {
        const ClearColor c{0.0, 0.5, 0.5, 1.0};
        const auto       px = target.clear_and_read_pixel(c, 0, 0);
        CHECK(near_equal(px[0], linear_to_u8(c.r)));
        CHECK(near_equal(px[1], linear_to_u8(c.g)));
        CHECK(near_equal(px[2], linear_to_u8(c.b)));
        CHECK(near_equal(px[3], linear_to_u8(c.a)));
    }

    SUBCASE("clear is uniform across all pixels") {
        const ClearColor c{0.25, 0.5, 0.75, 1.0};
        const auto bytes = target.clear_and_read(c);
        REQUIRE(bytes.size() == static_cast<std::size_t>(w) * h * 4);
        const int er = linear_to_u8(c.r);
        const int eg = linear_to_u8(c.g);
        const int eb = linear_to_u8(c.b);
        const int ea = linear_to_u8(c.a);
        for (std::size_t i = 0; i < bytes.size(); i += 4) {
            REQUIRE(near_equal(bytes[i + 0], er));
            REQUIRE(near_equal(bytes[i + 1], eg));
            REQUIRE(near_equal(bytes[i + 2], eb));
            REQUIRE(near_equal(bytes[i + 3], ea));
        }
    }
}
