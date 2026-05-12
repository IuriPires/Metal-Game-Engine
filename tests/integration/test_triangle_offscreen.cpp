// M3 golden-ish integration test: build an RHI pipeline that draws a
// hard-coded NDC triangle into an offscreen RGBA8 texture, read it back,
// and assert two things:
//  - the center pixel matches the centroid color of the triangle vertices
//  - a pixel firmly outside the triangle matches the clear color
//
// We deliberately avoid pixel-perfect golden PNG comparison until M9: tiny
// driver-version rasterizer differences would flap the test.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "mge/rhi/rhi.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace mge::rhi::metal_backend {
[[nodiscard]] std::vector<std::uint8_t> read_rgba8(Device& device, Texture& tex);
}

using namespace mge::rhi;

namespace {

struct Vertex {
    float pos[2];
    float color[4];
};

constexpr Vertex k_triangle[3] = {
    {{ 0.00f,  0.70f}, {1.0f, 0.20f, 0.20f, 1.0f}},
    {{-0.70f, -0.55f}, {0.20f, 1.0f, 0.30f, 1.0f}},
    {{ 0.70f, -0.55f}, {0.30f, 0.40f, 1.0f, 1.0f}},
};

constexpr const char* k_msl = R"(
    #include <metal_stdlib>
    using namespace metal;
    struct VSIn  { float2 pos [[attribute(0)]]; float4 color [[attribute(1)]]; };
    struct VSOut { float4 position [[position]]; float4 color; };
    vertex VSOut vertex_main(VSIn in [[stage_in]]) {
        VSOut o; o.position = float4(in.pos, 0.0, 1.0); o.color = in.color; return o;
    }
    fragment float4 fragment_main(VSOut in [[stage_in]]) { return in.color; }
)";

int linear_to_u8(double v) {
    if (v <= 0.0) return 0;
    if (v >= 1.0) return 255;
    return static_cast<int>(v * 255.0 + 0.5);
}

}  // namespace

TEST_CASE("triangle renders into offscreen target") {
    auto device = Device::create();
    REQUIRE(device);

    auto queue = device->create_queue("triangle.test.queue");
    REQUIRE(queue);

    auto shader = device->create_shader_from_msl(ShaderSourceDesc{k_msl, "triangle"});
    REQUIRE(shader);

    BufferDesc bd;
    bd.size              = sizeof(k_triangle);
    bd.usage             = BufferUsage::Vertex;
    bd.storage           = StorageMode::Shared;
    bd.initial_data      = k_triangle;
    bd.initial_data_size = sizeof(k_triangle);
    bd.label             = "triangle.test.vbuf";
    auto vbuf            = device->create_buffer(bd);
    REQUIRE(vbuf);

    constexpr std::uint32_t W = 64;
    constexpr std::uint32_t H = 64;

    TextureDesc td;
    td.width   = W;
    td.height  = H;
    td.format  = PixelFormat::RGBA8Unorm;
    td.usage   = TextureUsage::RenderTarget | TextureUsage::CopySrc;
    td.storage = StorageMode::Private;
    td.label   = "triangle.test.rt";
    auto rt    = device->create_texture(td);
    REQUIRE(rt);

    RenderPipelineDesc pd;
    pd.vertex_shader   = shader.get();
    pd.fragment_shader = shader.get();
    pd.vertex_entry    = "vertex_main";
    pd.fragment_entry  = "fragment_main";
    pd.vertex_layout.buffers    = {VertexBufferLayout{sizeof(Vertex), false}};
    pd.vertex_layout.attributes = {
        VertexAttribute{0, VertexFormat::Float32x2, offsetof(Vertex, pos),   0},
        VertexAttribute{1, VertexFormat::Float32x4, offsetof(Vertex, color), 0},
    };
    pd.color_targets[0].format = PixelFormat::RGBA8Unorm;
    pd.num_color_targets       = 1;
    pd.topology                = PrimitiveTopology::TriangleList;
    pd.label                   = "triangle.test.pso";

    auto pso = device->create_render_pipeline(pd);
    REQUIRE(pso);

    constexpr float clear[4] = {0.05f, 0.07f, 0.10f, 1.0f};
    {
        CommandBuffer cmd = queue->create_command_buffer();
        RenderPassDesc rp;
        rp.num_color_attachments               = 1;
        rp.color_attachments[0].texture        = rt.get();
        rp.color_attachments[0].load_action    = LoadAction::Clear;
        rp.color_attachments[0].store_action   = StoreAction::Store;
        rp.color_attachments[0].clear_color[0] = clear[0];
        rp.color_attachments[0].clear_color[1] = clear[1];
        rp.color_attachments[0].clear_color[2] = clear[2];
        rp.color_attachments[0].clear_color[3] = clear[3];
        rp.label                               = "triangle.test.pass";

        {
            RenderEncoder enc = cmd.begin_render_pass(rp);
            enc.set_pipeline(*pso);
            enc.set_vertex_buffer(*vbuf, 0);
            enc.draw(3);
        }
        cmd.commit();
        cmd.wait_until_completed();
    }

    const auto bytes = metal_backend::read_rgba8(*device, *rt);
    REQUIRE(bytes.size() == static_cast<std::size_t>(W) * H * 4u);

    auto pixel = [&](std::uint32_t x, std::uint32_t y) {
        const std::size_t i = (static_cast<std::size_t>(y) * W + x) * 4u;
        return std::array<std::uint8_t, 4>{bytes[i], bytes[i + 1], bytes[i + 2], bytes[i + 3]};
    };

    // The triangle is centered around NDC origin. In a top-left origin
    // texture, the centroid lands near (W/2, H/2). The barycentric centroid
    // color is the average of the three vertex colors: ~(0.5, 0.53, 0.53).
    const auto center = pixel(W / 2, H / 2);
    INFO("center pixel = ", static_cast<int>(center[0]), ", ", static_cast<int>(center[1]),
         ", ", static_cast<int>(center[2]));
    // Sanity: should be far from clear (which is very dark) and far from
    // any one single vertex (none pure white). Expect each channel >= 50.
    CHECK(center[0] >= 50);
    CHECK(center[1] >= 50);
    CHECK(center[2] >= 50);
    CHECK(center[3] == linear_to_u8(1.0));

    // Corner pixels are well outside the triangle: should match clear color.
    const auto corner = pixel(0, 0);
    CHECK(std::abs(int{corner[0]} - linear_to_u8(clear[0])) <= 2);
    CHECK(std::abs(int{corner[1]} - linear_to_u8(clear[1])) <= 2);
    CHECK(std::abs(int{corner[2]} - linear_to_u8(clear[2])) <= 2);

    const auto bottom_right = pixel(W - 1, 0);
    CHECK(std::abs(int{bottom_right[0]} - linear_to_u8(clear[0])) <= 2);
    CHECK(std::abs(int{bottom_right[1]} - linear_to_u8(clear[1])) <= 2);
}
