// M4 integration: render the procedural Lambert cube into an offscreen
// RGBA8 target and assert two things:
//  - the center of the image has a non-clear, plausible-looking color
//    (cube visible)
//  - at least one corner of the image matches the clear color (cube does
//    not fill the whole framebuffer)

#include "mge/assets/mesh.h"
#include "mge/assets/primitives.h"
#include "mge/math/mat.h"
#include "mge/math/vec.h"
#include "mge/rhi/rhi.h"
#include "mge/scene/camera.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <cstring>

namespace mge::rhi::metal_backend {
[[nodiscard]] std::vector<std::uint8_t> read_rgba8(Device& device, Texture& tex);
}

using namespace mge::rhi;
using namespace mge::math;

namespace {

struct alignas(16) LambertUniforms {
    Mat4 view_proj;
    Mat4 model;
    Mat4 model_inv_t;
    float light_dir_ws[4];
    float light_color[4];
    float ambient[4];
};

constexpr const char* k_msl = R"(
    #include <metal_stdlib>
    using namespace metal;

    struct Uniforms {
        float4x4 view_proj;
        float4x4 model;
        float4x4 model_inv_t;
        float4   light_dir_ws;
        float4   light_color;
        float4   ambient;
    };

    struct VSIn  { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]]; float4 color [[attribute(2)]]; };
    struct VSOut { float4 position [[position]]; float3 normal_ws; float4 color; };

    vertex VSOut vertex_main(VSIn in [[stage_in]],
                              device const Uniforms& u [[buffer(1)]]) {
        VSOut o;
        float4 world = u.model * float4(in.position, 1.0);
        o.position   = u.view_proj * world;
        o.normal_ws  = normalize((u.model_inv_t * float4(in.normal, 0.0)).xyz);
        o.color      = in.color;
        return o;
    }

    fragment float4 fragment_main(VSOut in [[stage_in]],
                                   device const Uniforms& u [[buffer(0)]]) {
        float  n_dot_l = max(dot(in.normal_ws, -u.light_dir_ws.xyz), 0.0);
        float3 diffuse = u.light_color.xyz * n_dot_l;
        float3 lit     = in.color.rgb * (diffuse + u.ambient.xyz);
        return float4(lit, 1.0);
    }
)";

}  // namespace

TEST_CASE("Lambert cube renders into an offscreen RT") {
    auto device = Device::create();
    REQUIRE(device);
    auto queue = device->create_queue("test");
    REQUIRE(queue);
    auto shader = device->create_shader_from_msl(ShaderSourceDesc{k_msl, "lambert"});
    REQUIRE(shader);

    const auto mesh = mge::assets::make_cube({0.8f, 0.6f, 0.3f, 1.0f});

    BufferDesc vb;
    vb.size              = mesh.vertices.size() * sizeof(mge::assets::LambertVertex);
    vb.usage             = BufferUsage::Vertex;
    vb.storage           = StorageMode::Shared;
    vb.initial_data      = mesh.vertices.data();
    vb.initial_data_size = vb.size;
    auto vbuf            = device->create_buffer(vb);
    REQUIRE(vbuf);

    BufferDesc ib;
    ib.size              = mesh.indices.size() * sizeof(std::uint32_t);
    ib.usage             = BufferUsage::Index;
    ib.storage           = StorageMode::Shared;
    ib.initial_data      = mesh.indices.data();
    ib.initial_data_size = ib.size;
    auto ibuf            = device->create_buffer(ib);
    REQUIRE(ibuf);

    BufferDesc ub;
    ub.size    = sizeof(LambertUniforms);
    ub.usage   = BufferUsage::Uniform;
    ub.storage = StorageMode::Shared;
    auto ubuf  = device->create_buffer(ub);
    REQUIRE(ubuf);

    constexpr std::uint32_t W = 128;
    constexpr std::uint32_t H = 128;

    TextureDesc cd;
    cd.width   = W;
    cd.height  = H;
    cd.format  = PixelFormat::RGBA8Unorm;
    cd.usage   = TextureUsage::RenderTarget | TextureUsage::CopySrc;
    cd.storage = StorageMode::Private;
    auto color = device->create_texture(cd);
    REQUIRE(color);

    TextureDesc dd;
    dd.width   = W;
    dd.height  = H;
    dd.format  = PixelFormat::Depth32Float;
    dd.usage   = TextureUsage::RenderTarget;
    dd.storage = StorageMode::Private;
    auto depth = device->create_texture(dd);
    REQUIRE(depth);

    RenderPipelineDesc pd;
    pd.vertex_shader   = shader.get();
    pd.fragment_shader = shader.get();
    pd.vertex_entry    = "vertex_main";
    pd.fragment_entry  = "fragment_main";
    pd.vertex_layout.buffers    = {VertexBufferLayout{sizeof(mge::assets::LambertVertex), false}};
    pd.vertex_layout.attributes = {
        VertexAttribute{0, VertexFormat::Float32x3, offsetof(mge::assets::LambertVertex, position), 0},
        VertexAttribute{1, VertexFormat::Float32x3, offsetof(mge::assets::LambertVertex, normal),   0},
        VertexAttribute{2, VertexFormat::Float32x4, offsetof(mge::assets::LambertVertex, color),    0},
    };
    pd.color_targets[0].format = PixelFormat::RGBA8Unorm;
    pd.num_color_targets       = 1;
    pd.depth.format            = PixelFormat::Depth32Float;
    pd.depth.write_enabled     = true;
    pd.depth.compare           = DepthCompare::Less;
    pd.rasterizer.cull_mode    = CullMode::Back;
    pd.rasterizer.front_face   = FrontFace::CounterClockwise;
    auto pso                   = device->create_render_pipeline(pd);
    REQUIRE(pso);

    mge::scene::Camera cam;
    cam.set_perspective(radians(45.0f), 1.0f, 0.1f, 50.0f);
    cam.look_at({2.5f, 1.8f, 3.0f}, {0, 0, 0}, {0, 1, 0});

    LambertUniforms u{};
    u.view_proj   = cam.view_projection();
    u.model       = Mat4::identity();
    u.model_inv_t = Mat4::identity();
    const Vec3 ld = normalize(Vec3{-0.5f, -0.7f, -0.5f});
    u.light_dir_ws[0] = ld.x;
    u.light_dir_ws[1] = ld.y;
    u.light_dir_ws[2] = ld.z;
    u.light_dir_ws[3] = 1.0f;
    u.light_color[0]  = 1.0f;
    u.light_color[1]  = 0.95f;
    u.light_color[2]  = 0.85f;
    u.light_color[3]  = 1.0f;
    u.ambient[0]      = 0.1f;
    u.ambient[1]      = 0.12f;
    u.ambient[2]      = 0.16f;
    u.ambient[3]      = 0.0f;
    std::memcpy(ubuf->contents(), &u, sizeof(u));

    constexpr float clear_rgb[3] = {0.04f, 0.06f, 0.09f};

    {
        CommandBuffer cmd = queue->create_command_buffer();
        RenderPassDesc rp;
        rp.num_color_attachments               = 1;
        rp.color_attachments[0].texture        = color.get();
        rp.color_attachments[0].load_action    = LoadAction::Clear;
        rp.color_attachments[0].store_action   = StoreAction::Store;
        rp.color_attachments[0].clear_color[0] = clear_rgb[0];
        rp.color_attachments[0].clear_color[1] = clear_rgb[1];
        rp.color_attachments[0].clear_color[2] = clear_rgb[2];
        rp.color_attachments[0].clear_color[3] = 1.0f;
        rp.has_depth                           = true;
        rp.depth_attachment.texture            = depth.get();
        rp.depth_attachment.load_action        = LoadAction::Clear;
        rp.depth_attachment.store_action       = StoreAction::DontCare;
        rp.depth_attachment.clear_depth        = 1.0f;
        {
            RenderEncoder enc = cmd.begin_render_pass(rp);
            enc.set_pipeline(*pso);
            enc.set_vertex_buffer(*vbuf, 0);
            enc.set_vertex_buffer(*ubuf, 1);
            enc.set_fragment_buffer(*ubuf, 0);
            enc.draw_indexed(static_cast<std::uint32_t>(mesh.indices.size()),
                              IndexType::UInt32, *ibuf);
        }
        cmd.commit();
        cmd.wait_until_completed();
    }

    const auto bytes = mge::rhi::metal_backend::read_rgba8(*device, *color);
    REQUIRE(bytes.size() == static_cast<std::size_t>(W) * H * 4u);

    auto pixel = [&](std::uint32_t x, std::uint32_t y) {
        const std::size_t i = (static_cast<std::size_t>(y) * W + x) * 4u;
        return std::array<std::uint8_t, 4>{bytes[i], bytes[i + 1], bytes[i + 2], bytes[i + 3]};
    };

    auto is_clear = [&](std::array<std::uint8_t, 4> p) {
        return std::abs(int{p[0]} - int(clear_rgb[0] * 255 + 0.5f)) <= 2 &&
               std::abs(int{p[1]} - int(clear_rgb[1] * 255 + 0.5f)) <= 2 &&
               std::abs(int{p[2]} - int(clear_rgb[2] * 255 + 0.5f)) <= 2;
    };

    // Center pixel should NOT be clear (the cube is roughly centered).
    const auto center = pixel(W / 2, H / 2);
    INFO("center pixel = ", static_cast<int>(center[0]), ", ",
         static_cast<int>(center[1]), ", ", static_cast<int>(center[2]));
    CHECK_FALSE(is_clear(center));

    // Cube has warm base color (0.8, 0.6, 0.3); after Lambert it stays
    // roughly that hue, so red > blue.
    CHECK(center[0] > center[2]);

    // The cube doesn't span the entire 128x128 framebuffer at this camera
    // distance; at least one corner pixel should still be the clear color.
    const auto tl = pixel(0, 0);
    const auto tr = pixel(W - 1, 0);
    const auto bl = pixel(0, H - 1);
    const auto br = pixel(W - 1, H - 1);
    CHECK((is_clear(tl) || is_clear(tr) || is_clear(bl) || is_clear(br)));
}
