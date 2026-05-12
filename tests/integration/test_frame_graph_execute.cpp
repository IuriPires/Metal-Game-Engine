// End-to-end frame graph: declares a single clear-only pass that writes an
// offscreen texture; verifies the pixels via the backend readback helper.
// This exercises the full compile -> execute path including command buffer
// creation and resource realization.

#include "mge/frame_graph/frame_graph.h"
#include "mge/rhi/rhi.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace mge::rhi::metal_backend {
[[nodiscard]] std::vector<std::uint8_t> read_rgba8(Device& device, Texture& tex);
}

using namespace mge::rhi;
using namespace mge::frame_graph;

namespace {

int u8(float v) {
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 255;
    return static_cast<int>(v * 255.0f + 0.5f);
}

}  // namespace

TEST_CASE("FrameGraph executes a single clear pass") {
    auto device = Device::create();
    REQUIRE(device);
    auto queue = device->create_queue("fg.test.queue");
    REQUIRE(queue);

    constexpr std::uint32_t W = 32;
    constexpr std::uint32_t H = 32;

    TextureDesc rtd;
    rtd.width   = W;
    rtd.height  = H;
    rtd.format  = PixelFormat::RGBA8Unorm;
    rtd.usage   = TextureUsage::RenderTarget | TextureUsage::CopySrc;
    rtd.storage = StorageMode::Private;
    rtd.label   = "fg.test.target";
    auto target = device->create_texture(rtd);
    REQUIRE(target);

    FrameGraph fg(*device);
    auto backbuffer = fg.import_texture(*target, "backbuffer");

    const float r = 0.2f, g = 0.6f, b = 0.4f;
    fg.add_pass("clear",
        [&](PassBuilder& pb) {
            pb.write_color(backbuffer, LoadAction::Clear, r, g, b, 1.0f);
        },
        [&](RenderContext& ctx) {
            const auto rp = ctx.make_render_pass_desc();
            { auto enc = ctx.cmd().begin_render_pass(rp); }
        });

    REQUIRE(fg.compile());
    fg.execute(*queue);
    // Wait for GPU.
    {
        auto cmd = queue->create_command_buffer();
        cmd.commit();
        cmd.wait_until_completed();
    }

    const auto bytes = metal_backend::read_rgba8(*device, *target);
    REQUIRE(bytes.size() == static_cast<std::size_t>(W) * H * 4u);
    for (std::size_t i = 0; i < bytes.size(); i += 4) {
        CHECK(std::abs(int{bytes[i + 0]} - u8(r)) <= 2);
        CHECK(std::abs(int{bytes[i + 1]} - u8(g)) <= 2);
        CHECK(std::abs(int{bytes[i + 2]} - u8(b)) <= 2);
    }
}
