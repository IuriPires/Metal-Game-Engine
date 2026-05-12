// Ray-tracing smoke test:
//   1. Build a BLAS over a single triangle (3 verts, 1 index list of 3).
//   2. Build a TLAS with one instance pointing at that BLAS.
//   3. Render a fullscreen triangle into a 64x64 RGBA8 target. The fragment
//      shader fires a ray straight down (-Z) from the screen plane against
//      the TLAS and outputs green on hit, red on miss.
//   4. Center pixel should be green (hits the triangle); corner pixels should
//      be red (miss).

#include "mge/rhi/rhi.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace mge::rhi::metal_backend {
[[nodiscard]] std::vector<std::uint8_t> read_rgba8(Device& device, Texture& tex);
}

using namespace mge::rhi;

namespace {

// Fullscreen triangle that fires a ray from (uv.x, uv.y, +1) toward -Z and
// colors the pixel green on hit / red on miss.
constexpr const char* k_rt_msl = R"(
    #include <metal_stdlib>
    #include <metal_raytracing>
    using namespace metal;
    using namespace raytracing;

    struct VSOut { float4 position [[position]]; float2 uv; };

    vertex VSOut rt_vs(uint vid [[vertex_id]]) {
        float2 uv = float2((vid << 1) & 2, vid & 2);
        VSOut o;
        // Map (0..2, 0..2) → NDC (-1..3, 1..-3) for a fullscreen triangle.
        o.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
        o.position.y = -o.position.y;
        o.uv = uv;
        return o;
    }

    fragment float4 rt_fs(VSOut in [[stage_in]],
                            instance_acceleration_structure tlas [[buffer(0)]]) {
        // Map uv (0..1) to xy in [-1, +1]; ray starts above the triangle and
        // shoots toward -Z. Triangle lives at z=0 spanning roughly [-1,+1]^2.
        float3 origin    = float3(in.uv.x * 2.0 - 1.0,
                                    in.uv.y * 2.0 - 1.0,
                                    1.0);
        float3 direction = float3(0.0, 0.0, -1.0);

        ray r;
        r.origin       = origin;
        r.direction    = direction;
        r.min_distance = 0.0;
        r.max_distance = 10.0;

        intersector<instancing> isect;
        isect.assume_geometry_type(geometry_type::triangle);
        auto result = isect.intersect(r, tlas);

        if (result.type == intersection_type::triangle) {
            return float4(0.0, 1.0, 0.0, 1.0);
        }
        return float4(1.0, 0.0, 0.0, 1.0);
    }
)";

}  // namespace

TEST_CASE("RT inline ray query hits a single-triangle BVH") {
    auto device = Device::create();
    REQUIRE(device);
    const auto info = device->info();
    if (!info.supports_ray_tracing || !info.supports_ray_tracing_from_render) {
        WARN("device does not support ray tracing from render — skipping");
        return;
    }

    auto queue = device->create_queue("rt.test.queue");
    REQUIRE(queue);

    // 1) Single right triangle at z=0 covering roughly the central half of
    //    the [-1,+1] square (vertices at (-0.5, -0.5), (0.5, -0.5), (0, 0.5)).
    const float verts[9] = {
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
         0.0f,  0.5f, 0.0f,
    };
    const std::uint32_t indices[3] = {0u, 1u, 2u};

    BufferDesc vbd;
    vbd.size              = sizeof(verts);
    vbd.usage             = BufferUsage::Vertex | BufferUsage::Storage;
    vbd.storage           = StorageMode::Shared;
    vbd.initial_data      = verts;
    vbd.initial_data_size = sizeof(verts);
    vbd.label             = "rt.tri.vbuf";
    auto vbuf = device->create_buffer(vbd);
    REQUIRE(vbuf);

    BufferDesc ibd;
    ibd.size              = sizeof(indices);
    ibd.usage             = BufferUsage::Index | BufferUsage::Storage;
    ibd.storage           = StorageMode::Shared;
    ibd.initial_data      = indices;
    ibd.initial_data_size = sizeof(indices);
    ibd.label             = "rt.tri.ibuf";
    auto ibuf = device->create_buffer(ibd);
    REQUIRE(ibuf);

    // 2) BLAS containing the triangle.
    TriangleGeometryDesc tg;
    tg.vertex_buffer  = vbuf.get();
    tg.vertex_stride  = sizeof(float) * 3;
    tg.vertex_count   = 3;
    tg.vertex_format  = VertexFormat::Float32x3;
    tg.index_buffer   = ibuf.get();
    tg.index_type     = IndexType::UInt32;
    tg.triangle_count = 1;
    tg.opaque         = true;
    PrimitiveAccelDesc bd;
    bd.geometries.push_back(tg);
    bd.label = "rt.blas";
    auto blas = device->build_acceleration_structure(*queue, bd);
    REQUIRE(blas);

    // 3) TLAS with one identity-transform instance referencing the BLAS.
    AccelInstance inst;
    inst.transform_3x4 = {1.0f, 0.0f, 0.0f, 0.0f,
                          0.0f, 1.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 1.0f, 0.0f};
    inst.blas_index = 0;
    inst.mask       = 0xFFu;
    inst.opaque     = true;
    InstanceAccelDesc id;
    id.blas      = {blas.get()};
    id.instances = {inst};
    id.label     = "rt.tlas";
    auto tlas = device->build_acceleration_structure(*queue, id);
    REQUIRE(tlas);

    // 4) Build the RT render pipeline.
    auto shader = device->create_shader_from_msl({k_rt_msl, "rt"});
    REQUIRE(shader);

    constexpr std::uint32_t W = 64;
    constexpr std::uint32_t H = 64;
    constexpr PixelFormat   fmt = PixelFormat::RGBA8Unorm;

    TextureDesc td;
    td.width   = W;
    td.height  = H;
    td.format  = fmt;
    td.usage   = TextureUsage::RenderTarget | TextureUsage::CopySrc;
    td.storage = StorageMode::Private;
    td.label   = "rt.rt";
    auto rt = device->create_texture(td);
    REQUIRE(rt);

    RenderPipelineDesc pd;
    pd.vertex_shader            = shader.get();
    pd.fragment_shader          = shader.get();
    pd.vertex_entry             = "rt_vs";
    pd.fragment_entry           = "rt_fs";
    pd.topology                 = PrimitiveTopology::TriangleList;
    pd.color_targets[0].format  = fmt;
    pd.color_targets[0].blend   = false;
    pd.num_color_targets        = 1;
    pd.rasterizer.cull_mode     = CullMode::None;
    pd.label                    = "rt.pso";
    auto pso = device->create_render_pipeline(pd);
    REQUIRE(pso);

    // 5) Encode + run.
    {
        auto cmd = queue->create_command_buffer();
        RenderPassDesc rp;
        rp.num_color_attachments               = 1;
        rp.color_attachments[0].texture        = rt.get();
        rp.color_attachments[0].load_action    = LoadAction::Clear;
        rp.color_attachments[0].store_action   = StoreAction::Store;
        rp.color_attachments[0].clear_color[0] = 0.0f;
        rp.color_attachments[0].clear_color[1] = 0.0f;
        rp.color_attachments[0].clear_color[2] = 0.0f;
        rp.color_attachments[0].clear_color[3] = 1.0f;
        rp.label = "rt.pass";

        auto enc = cmd.begin_render_pass(rp);
        enc.set_pipeline(*pso);
        enc.use_fragment_acceleration_structure(*blas);
        enc.set_fragment_acceleration_structure(*tlas, 0);
        enc.draw(3);
        enc.end();
        cmd.commit();
        cmd.wait_until_completed();
    }

    const auto pixels = mge::rhi::metal_backend::read_rgba8(*device, *rt);
    REQUIRE(pixels.size() == W * H * 4u);

    auto at = [&](std::uint32_t x, std::uint32_t y) {
        return &pixels[(y * W + x) * 4u];
    };
    // Center should be green (hit), corners red (miss).
    const auto* center = at(W / 2u, H / 2u);
    CHECK(center[0] < 32);   // r low
    CHECK(center[1] > 220);  // g high
    CHECK(center[2] < 32);   // b low

    const auto* tl = at(1, 1);
    CHECK(tl[0] > 220);  // r high (miss)
    CHECK(tl[1] < 32);
    CHECK(tl[2] < 32);
}
