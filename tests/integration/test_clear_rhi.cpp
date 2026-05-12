// RHI clear test: create a Metal device through the RHI, allocate an
// offscreen render target, encode a clear-only render pass, read back the
// pixels via the backend's blit helper, assert the color.

#include "mge/rhi/rhi.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>

namespace mge::rhi::metal_backend {
[[nodiscard]] std::vector<std::uint8_t> read_rgba8(Device& device, Texture& tex);
}

using namespace mge::rhi;

namespace {

constexpr int tol = 1;

int linear_to_u8(double v) {
    if (v <= 0.0) return 0;
    if (v >= 1.0) return 255;
    return static_cast<int>(v * 255.0 + 0.5);
}

bool near_equal(int a, int b) {
    return std::abs(a - b) <= tol;
}

}  // namespace

TEST_CASE("RHI Device can be created") {
    auto device = Device::create();
    REQUIRE(device);
    const auto info = device->info();
    INFO("device: ", info.name);
    CHECK_FALSE(info.name.empty());
}

TEST_CASE("RHI clear pass produces the expected color") {
    auto device = Device::create();
    REQUIRE(device);
    auto queue = device->create_queue("test.queue");
    REQUIRE(queue);

    TextureDesc td;
    td.width   = 16;
    td.height  = 16;
    td.format  = PixelFormat::RGBA8Unorm;
    td.usage   = TextureUsage::RenderTarget | TextureUsage::CopySrc;
    td.storage = StorageMode::Private;
    td.label   = "test.rt";
    auto tex   = device->create_texture(td);
    REQUIRE(tex);

    const float r = 0.25f, g = 0.5f, b = 0.75f, a = 1.0f;

    {
        CommandBuffer cmd = queue->create_command_buffer();
        RenderPassDesc rp;
        rp.num_color_attachments = 1;
        rp.color_attachments[0].texture        = tex.get();
        rp.color_attachments[0].load_action    = LoadAction::Clear;
        rp.color_attachments[0].store_action   = StoreAction::Store;
        rp.color_attachments[0].clear_color[0] = r;
        rp.color_attachments[0].clear_color[1] = g;
        rp.color_attachments[0].clear_color[2] = b;
        rp.color_attachments[0].clear_color[3] = a;
        rp.label                               = "test.clear";
        { auto enc = cmd.begin_render_pass(rp); }
        cmd.commit();
        cmd.wait_until_completed();
    }

    const auto bytes = metal_backend::read_rgba8(*device, *tex);
    REQUIRE(bytes.size() == 16u * 16u * 4u);

    const int er = linear_to_u8(r);
    const int eg = linear_to_u8(g);
    const int eb = linear_to_u8(b);
    const int ea = linear_to_u8(a);
    for (std::size_t i = 0; i < bytes.size(); i += 4) {
        REQUIRE(near_equal(bytes[i + 0], er));
        REQUIRE(near_equal(bytes[i + 1], eg));
        REQUIRE(near_equal(bytes[i + 2], eb));
        REQUIRE(near_equal(bytes[i + 3], ea));
    }
}
