// HZB build smoke test:
//   Stamp a known depth pattern into a small R32Float source texture (manually,
//   via a Shared staging buffer + blit-style copy through a fragment pass).
//   Then run a 2-thread max-reduce style HZB build that downsamples the
//   source to a destination. Validate the destination is the max of the
//   source tile.

#include "mge/rhi/rhi.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mge::rhi::metal_backend {
[[nodiscard]] std::vector<std::uint8_t> read_rgba8(Device& device, Texture& tex);
}

using namespace mge::rhi;

namespace {

// Kernel: read a 4x4 R32F texture, write the max of each 2x2 tile into a
// 2x2 R32F destination. Mirrors the per-thread tile-max pattern used by the
// hello_metal demo's hzb_build kernel.
constexpr const char* k_hzb_test_msl = R"(
    #include <metal_stdlib>
    using namespace metal;

    struct U { uint src_w; uint src_h; uint dst_w; uint dst_h; };

    kernel void hzb_reduce(texture2d<float, access::read>  src [[texture(0)]],
                            texture2d<float, access::write> dst [[texture(1)]],
                            constant U&                     u   [[buffer(0)]],
                            uint2 gid [[thread_position_in_grid]]) {
        if (gid.x >= u.dst_w || gid.y >= u.dst_h) return;
        uint sx0 = (gid.x       * u.src_w) / u.dst_w;
        uint sx1 = ((gid.x + 1) * u.src_w) / u.dst_w;
        uint sy0 = (gid.y       * u.src_h) / u.dst_h;
        uint sy1 = ((gid.y + 1) * u.src_h) / u.dst_h;
        float m = 0.0;
        for (uint y = sy0; y < sy1; ++y) {
            for (uint x = sx0; x < sx1; ++x) {
                m = max(m, src.read(uint2(x, y)).r);
            }
        }
        dst.write(float4(m), gid);
    }

    // Tiny fragment-shader populator for the source texture: each pixel gets
    // a known value derived from its UV so we can check the reduce result.
    struct VSOut { float4 position [[position]]; float2 uv; };

    vertex VSOut fill_vs(uint vid [[vertex_id]]) {
        float2 uv = float2((vid << 1) & 2, vid & 2);
        VSOut o;
        o.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
        o.position.y = -o.position.y;
        o.uv = uv;
        return o;
    }

    fragment float4 fill_fs(VSOut in [[stage_in]]) {
        // Pattern: depth = (x + y) / (W + H), max at bottom-right.
        // Multiplied so we can see distinct tile maxes.
        return float4(in.uv.x * 0.5 + in.uv.y * 0.5, 0, 0, 1);
    }
)";

}  // namespace

TEST_CASE("HZB compute reduce produces the per-tile maximum") {
    auto device = Device::create();
    REQUIRE(device);
    auto queue = device->create_queue("hzb.test.queue");
    REQUIRE(queue);

    constexpr std::uint32_t Wsrc = 4, Hsrc = 4;
    constexpr std::uint32_t Wdst = 2, Hdst = 2;

    // 1) Fill source texture (R32F) with a known gradient via a fragment pass.
    auto shader = device->create_shader_from_msl({k_hzb_test_msl, "hzb.test"});
    REQUIRE(shader);

    TextureDesc src_d;
    src_d.width   = Wsrc;
    src_d.height  = Hsrc;
    src_d.format  = PixelFormat::R32Float;
    src_d.usage   = TextureUsage::RenderTarget | TextureUsage::ShaderRead;
    src_d.storage = StorageMode::Private;
    src_d.label   = "hzb.src";
    auto src_tex = device->create_texture(src_d);
    REQUIRE(src_tex);

    TextureDesc dst_d = src_d;
    dst_d.width   = Wdst;
    dst_d.height  = Hdst;
    dst_d.usage   = TextureUsage::ShaderWrite | TextureUsage::ShaderRead |
                     TextureUsage::CopySrc;
    dst_d.label   = "hzb.dst";
    auto dst_tex = device->create_texture(dst_d);
    REQUIRE(dst_tex);

    RenderPipelineDesc fill_pd;
    fill_pd.vertex_shader            = shader.get();
    fill_pd.fragment_shader          = shader.get();
    fill_pd.vertex_entry             = "fill_vs";
    fill_pd.fragment_entry           = "fill_fs";
    fill_pd.color_targets[0].format  = PixelFormat::R32Float;
    fill_pd.num_color_targets        = 1;
    fill_pd.rasterizer.cull_mode     = CullMode::None;
    fill_pd.label                    = "hzb.fill.pso";
    auto fill_pso = device->create_render_pipeline(fill_pd);
    REQUIRE(fill_pso);

    ComputePipelineDesc reduce_pd;
    reduce_pd.compute_shader = shader.get();
    reduce_pd.compute_entry  = "hzb_reduce";
    reduce_pd.label          = "hzb.reduce.pso";
    auto reduce_pso = device->create_compute_pipeline(reduce_pd);
    REQUIRE(reduce_pso);

    struct U { std::uint32_t src_w, src_h, dst_w, dst_h; };
    U u{Wsrc, Hsrc, Wdst, Hdst};
    BufferDesc ub;
    ub.size              = sizeof(u);
    ub.usage             = BufferUsage::Uniform;
    ub.storage           = StorageMode::Shared;
    ub.initial_data      = &u;
    ub.initial_data_size = sizeof(u);
    ub.label             = "hzb.u";
    auto ubo = device->create_buffer(ub);
    REQUIRE(ubo);

    // 2) Encode: fill src via render, reduce src→dst via compute.
    {
        auto cmd = queue->create_command_buffer();
        {
            RenderPassDesc rp;
            rp.num_color_attachments               = 1;
            rp.color_attachments[0].texture        = src_tex.get();
            rp.color_attachments[0].load_action    = LoadAction::Clear;
            rp.color_attachments[0].store_action   = StoreAction::Store;
            rp.label = "hzb.fill";
            auto enc = cmd.begin_render_pass(rp);
            enc.set_pipeline(*fill_pso);
            enc.draw(3);
        }
        {
            auto enc = cmd.begin_compute_pass("hzb.reduce");
            enc.set_pipeline(*reduce_pso);
            enc.set_texture(*src_tex, 0);
            enc.set_texture(*dst_tex, 1);
            enc.set_buffer(*ubo, 0);
            enc.dispatch_threads(Wdst, Hdst, 1, 2, 2, 1);
        }
        cmd.commit();
        cmd.wait_until_completed();
    }

    // 3) Read back dst as R32F via a Shared staging buffer + blit.
    BufferDesc readback;
    readback.size    = Wdst * Hdst * sizeof(float);
    readback.usage   = BufferUsage::CopyDst | BufferUsage::Storage;
    readback.storage = StorageMode::Shared;
    readback.label   = "hzb.readback";
    auto rb = device->create_buffer(readback);
    REQUIRE(rb);

    // Copy dst → rb via a single-pass compute since the RHI has no blit
    // encoder. Reuse hzb_reduce with src_w=dst_w (identity).
    {
        // Build a tiny copy kernel inline to avoid blit dependency.
        constexpr const char* k_copy_msl = R"(
            #include <metal_stdlib>
            using namespace metal;
            kernel void copy_to_buf(texture2d<float, access::read> src [[texture(0)]],
                                      device float*               dst [[buffer(0)]],
                                      uint2 gid [[thread_position_in_grid]]) {
                uint w = src.get_width();
                dst[gid.y * w + gid.x] = src.read(gid).r;
            }
        )";
        auto copy_sh = device->create_shader_from_msl({k_copy_msl, "hzb.copy"});
        REQUIRE(copy_sh);
        ComputePipelineDesc cpd;
        cpd.compute_shader = copy_sh.get();
        cpd.compute_entry  = "copy_to_buf";
        cpd.label          = "hzb.copy.pso";
        auto copy_pso = device->create_compute_pipeline(cpd);
        REQUIRE(copy_pso);

        auto cmd = queue->create_command_buffer();
        auto enc = cmd.begin_compute_pass("hzb.copy");
        enc.set_pipeline(*copy_pso);
        enc.set_texture(*dst_tex, 0);
        enc.set_buffer(*rb, 0);
        enc.dispatch_threads(Wdst, Hdst, 1, 2, 2, 1);
        enc.end();
        cmd.commit();
        cmd.wait_until_completed();
    }

    const auto* out = static_cast<const float*>(rb->contents());
    // Source pattern: per-pixel value = (px + 0.5)/Wsrc * 0.5 + (py + 0.5)/Hsrc * 0.5
    // Each dst tile covers 2x2 src pixels. Max of the tile = max of those 4.
    auto src_value = [](std::uint32_t x, std::uint32_t y) {
        const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(Wsrc);
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(Hsrc);
        return u * 0.5f + v * 0.5f;
    };
    auto tile_max = [&](std::uint32_t tx, std::uint32_t ty) {
        const std::uint32_t x0 = (tx       * Wsrc) / Wdst;
        const std::uint32_t x1 = ((tx + 1) * Wsrc) / Wdst;
        const std::uint32_t y0 = (ty       * Hsrc) / Hdst;
        const std::uint32_t y1 = ((ty + 1) * Hsrc) / Hdst;
        float m = 0.0f;
        for (std::uint32_t y = y0; y < y1; ++y)
            for (std::uint32_t x = x0; x < x1; ++x)
                m = std::max(m, src_value(x, y));
        return m;
    };

    for (std::uint32_t y = 0; y < Hdst; ++y) {
        for (std::uint32_t x = 0; x < Wdst; ++x) {
            const float got      = out[y * Wdst + x];
            const float expected = tile_max(x, y);
            INFO("tile (", x, ",", y, ")  got=", got, "  expected=", expected);
            CHECK(std::abs(got - expected) < 1e-3f);
        }
    }
}
