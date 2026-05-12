// M9 demo: deferred PBR + shadow + instancing + frustum culling + post-FX.
// Post chain: bright extract -> 5-mip down/up bloom pyramid -> ACES tonemap.
//
// Scene now has three groups of instances on a 13-pass FrameGraph:
//   - 5 spheres (PBR material spectrum, instanced draw)
//   - 1 ground plane (instanced draw of count 1)
//   - NxN grid of small cubes around the spheres, CPU-side frustum-culled
//     each frame and re-uploaded into the instance buffer
//
// FrameConstants (view_proj, light_view_proj) are global to a pass. Per-
// instance data (model, model_inv_t, albedo, metallic, roughness) lives in
// one big instance buffer, sliced per draw.
//
// Args:
//   --frames N    run for N frames then exit
//   --headless    skip window creation
//   --width W
//   --height H
//   --cubes N     grid side count for the instanced cube field (default 32 -> 1024 cubes)

#include "mge/assets/pbr_mesh.h"
#include "mge/core/time.h"
#include "mge/core/version.h"
#include "mge/frame_graph/frame_graph.h"
#include "mge/math/aabb.h"
#include "mge/math/frustum.h"
#include "mge/math/mat.h"
#include "mge/math/vec.h"
#include "mge/platform/app.h"
#include "mge/platform/window.h"
#include "mge/rhi/rhi.h"
#include "mge/scene/camera.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Args {
    int           frames    = 0;
    bool          headless  = false;
    std::uint32_t width     = 1280;
    std::uint32_t height    = 720;
    std::uint32_t cube_side = 32;  // total cubes = side * side
};

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string_view s = argv[i];
        if (s == "--frames" && i + 1 < argc) {
            a.frames = std::atoi(argv[++i]);
        } else if (s == "--headless") {
            a.headless = true;
        } else if (s == "--width" && i + 1 < argc) {
            a.width = static_cast<std::uint32_t>(std::atoi(argv[++i]));
        } else if (s == "--height" && i + 1 < argc) {
            a.height = static_cast<std::uint32_t>(std::atoi(argv[++i]));
        } else if (s == "--cubes" && i + 1 < argc) {
            const int n = std::atoi(argv[++i]);
            a.cube_side = static_cast<std::uint32_t>(n > 0 ? n : 1);
        }
    }
    return a;
}

// ---------- Uniform layouts ----------

struct alignas(16) FrameConstants {
    mge::math::Mat4 view_proj;        // 64 B
    mge::math::Mat4 light_view_proj;  // 64 B
};
static_assert(sizeof(FrameConstants) == 128);

struct alignas(16) InstanceData {
    mge::math::Mat4 model;          // 64 B
    mge::math::Mat4 model_inv_t;    // 64 B
    float           albedo_ao[4];   // 16 B  rgb=albedo, a=ao
    float           mr[4];          // 16 B  r=metallic, g=roughness, ba=pad
};
static_assert(sizeof(InstanceData) == 160);

struct alignas(16) LightingConstants {
    mge::math::Mat4 view_proj_inv;
    mge::math::Mat4 light_view_proj;
    float           camera_ws[4];
    float           sun_dir_ws[4];
    float           sun_color[4];
    float           ambient[4];
    float           shadow_params[4];
};
static_assert(sizeof(LightingConstants) == 208);

// ---------- Inline MSL ----------

constexpr const char* k_shadow_msl = R"(
    #include <metal_stdlib>
    using namespace metal;

    struct InstanceData {
        float4x4 model;
        float4x4 model_inv_t;
        float4   albedo_ao;
        float4   mr;
    };
    struct FrameConstants {
        float4x4 view_proj;
        float4x4 light_view_proj;
    };

    struct VSIn  { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]]; };
    struct VSOut { float4 position [[position]]; };

    vertex VSOut shadow_vs(VSIn in [[stage_in]],
                            uint iid [[instance_id]],
                            device const InstanceData* instances [[buffer(1)]],
                            device const FrameConstants& fc [[buffer(2)]]) {
        VSOut o;
        o.position = fc.light_view_proj * instances[iid].model * float4(in.position, 1.0);
        return o;
    }
)";

constexpr const char* k_gbuffer_msl = R"(
    #include <metal_stdlib>
    using namespace metal;

    struct InstanceData {
        float4x4 model;
        float4x4 model_inv_t;
        float4   albedo_ao;
        float4   mr;
    };
    struct FrameConstants {
        float4x4 view_proj;
        float4x4 light_view_proj;
    };

    struct VSIn  { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]]; };
    struct VSOut {
        float4 position [[position]];
        float3 normal_ws;
        uint   iid [[flat]];
    };

    vertex VSOut gbuffer_vs(VSIn in [[stage_in]],
                             uint iid [[instance_id]],
                             device const InstanceData* instances [[buffer(1)]],
                             device const FrameConstants& fc [[buffer(2)]]) {
        VSOut o;
        const InstanceData inst = instances[iid];
        o.position  = fc.view_proj * inst.model * float4(in.position, 1.0);
        o.normal_ws = normalize((inst.model_inv_t * float4(in.normal, 0.0)).xyz);
        o.iid       = iid;
        return o;
    }

    float2 octa_encode(float3 n) {
        n /= (abs(n.x) + abs(n.y) + abs(n.z));
        float2 e = n.z >= 0.0 ? n.xy
                              : (1.0 - abs(n.yx)) * float2(n.x >= 0 ? 1 : -1, n.y >= 0 ? 1 : -1);
        return e;
    }

    struct GBufOut {
        float4 c0 [[color(0)]];
        float4 c1 [[color(1)]];
    };

    fragment GBufOut gbuffer_fs(VSOut in [[stage_in]],
                                 device const InstanceData* instances [[buffer(0)]]) {
        const InstanceData inst = instances[in.iid];
        GBufOut o;
        o.c0 = float4(inst.albedo_ao.rgb, inst.albedo_ao.a);
        float2 n = octa_encode(normalize(in.normal_ws));
        o.c1 = float4(n.x, n.y, inst.mr.g, inst.mr.r);
        return o;
    }
)";

constexpr const char* k_lighting_msl = R"(
    #include <metal_stdlib>
    using namespace metal;
    constant float PI = 3.14159265358979323846;

    struct LightingConstants {
        float4x4 view_proj_inv;
        float4x4 light_view_proj;
        float4   camera_ws;
        float4   sun_dir_ws;
        float4   sun_color;
        float4   ambient;
        float4   shadow_params;
    };

    struct VSOut { float4 position [[position]]; float2 uv; };

    vertex VSOut lighting_vs(uint vid [[vertex_id]]) {
        float2 uv = float2((vid << 1) & 2, vid & 2);
        VSOut o;
        o.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
        o.uv       = float2(uv.x, 1.0 - uv.y);
        return o;
    }

    float3 octa_decode(float2 e) {
        float3 n = float3(e, 1.0 - abs(e.x) - abs(e.y));
        if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * float2(n.x >= 0 ? 1 : -1, n.y >= 0 ? 1 : -1);
        return normalize(n);
    }

    float D_GGX(float NoH, float a2) {
        float f = (NoH * a2 - NoH) * NoH + 1.0;
        return a2 / (PI * f * f);
    }
    float V_SmithCorrelated(float NoV, float NoL, float a) {
        float a2 = a * a;
        float Lv = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
        float Lz = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
        return 0.5 / max(Lv + Lz, 1e-5);
    }
    float3 F_Schlick(float u, float3 F0) {
        return F0 + (1.0 - F0) * pow(1.0 - u, 5.0);
    }

    float sample_shadow(depth2d<float> shadow_map,
                         sampler          shadow_sampler,
                         float3           world_pos,
                         float4x4         light_view_proj,
                         float            texel_size,
                         float            bias) {
        float4 ls   = light_view_proj * float4(world_pos, 1.0);
        float3 lsn  = ls.xyz / ls.w;
        float2 uv   = lsn.xy * 0.5 + 0.5;
        uv.y        = 1.0 - uv.y;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
        if (lsn.z < 0.0 || lsn.z > 1.0) return 1.0;

        float ref = lsn.z - bias;
        float lit = 0.0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                float2 o = float2(float(dx), float(dy)) * texel_size;
                float  s = shadow_map.sample(shadow_sampler, uv + o);
                lit += (ref <= s) ? 1.0 : 0.0;
            }
        }
        return lit / 9.0;
    }

    fragment float4 lighting_fs(VSOut in [[stage_in]],
                                  texture2d<float> gb0 [[texture(0)]],
                                  texture2d<float> gb1 [[texture(1)]],
                                  depth2d<float>   dt  [[texture(2)]],
                                  depth2d<float>   sh  [[texture(3)]],
                                  sampler          s   [[sampler(0)]],
                                  sampler          sh_s [[sampler(1)]],
                                  device const LightingConstants& u [[buffer(0)]]) {
        float depth = dt.sample(s, in.uv);
        if (depth >= 1.0) discard_fragment();

        float4 ndc = float4(in.uv.x * 2.0 - 1.0,
                            1.0 - in.uv.y * 2.0,
                            depth, 1.0);
        float4 ws  = u.view_proj_inv * ndc;
        float3 P   = ws.xyz / ws.w;

        float4 c0 = gb0.sample(s, in.uv);
        float4 c1 = gb1.sample(s, in.uv);
        float3 albedo    = c0.rgb;
        float  ao        = c0.a;
        float3 N         = octa_decode(c1.xy);
        float  roughness = c1.z;
        float  metallic  = c1.w;

        float a  = roughness * roughness;
        float a2 = a * a;

        float3 V = normalize(u.camera_ws.xyz - P);
        float3 L = -normalize(u.sun_dir_ws.xyz);
        float3 H = normalize(V + L);
        float  NoL = saturate(dot(N, L));
        float  NoV = saturate(dot(N, V));
        float  NoH = saturate(dot(N, H));
        float  VoH = saturate(dot(V, H));

        float3 F0       = mix(float3(0.04), albedo, metallic);
        float3 F        = F_Schlick(VoH, F0);
        float  D        = D_GGX(NoH, a2);
        float  Vt       = V_SmithCorrelated(NoV, NoL, a);
        float3 specular = D * Vt * F;
        float3 kD       = (1.0 - F) * (1.0 - metallic);
        float3 diffuse  = kD * albedo / PI;

        float shadow = sample_shadow(sh, sh_s, P, u.light_view_proj,
                                      u.shadow_params.r, u.shadow_params.g);

        float3 direct  = (diffuse + specular) * u.sun_color.rgb * NoL * shadow;
        float3 ambient = u.ambient.rgb * albedo * ao;
        return float4(direct + ambient, 1.0);
    }
)";

constexpr const char* k_bright_msl = R"(
    #include <metal_stdlib>
    using namespace metal;

    struct VSOut { float4 position [[position]]; float2 uv; };

    vertex VSOut fullscreen_vs(uint vid [[vertex_id]]) {
        float2 uv = float2((vid << 1) & 2, vid & 2);
        VSOut o;
        o.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
        o.uv       = float2(uv.x, 1.0 - uv.y);
        return o;
    }

    // Soft-thresholded bright pass per Karis 2014. Pixels above `threshold` keep
    // their HDR colour, pixels just below get a quadratic ramp ("knee").
    fragment float4 bright_fs(VSOut in [[stage_in]],
                               texture2d<float> hdr [[texture(0)]],
                               sampler          s   [[sampler(0)]]) {
        float3 c = hdr.sample(s, in.uv).rgb;
        const float threshold = 1.0;
        const float knee      = 0.5;
        float brightness = max(c.r, max(c.g, c.b));
        float soft = max(0.0, brightness - threshold + knee);
        soft = soft * soft / (4.0 * knee + 1e-5);
        float contribution = max(soft, brightness - threshold) / max(brightness, 1e-5);
        return float4(c * contribution, 1.0);
    }
)";

constexpr const char* k_downsample_msl = R"(
    #include <metal_stdlib>
    using namespace metal;

    struct VSOut { float4 position [[position]]; float2 uv; };

    vertex VSOut fullscreen_vs(uint vid [[vertex_id]]) {
        float2 uv = float2((vid << 1) & 2, vid & 2);
        VSOut o;
        o.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
        o.uv       = float2(uv.x, 1.0 - uv.y);
        return o;
    }

    // 4-tap bilinear box filter at +/- half-texel offsets - leverages HW
    // bilinear so each sample averages 4 source texels.
    fragment float4 downsample_fs(VSOut in [[stage_in]],
                                   texture2d<float> src [[texture(0)]],
                                   sampler          s   [[sampler(0)]]) {
        float w = float(src.get_width());
        float h = float(src.get_height());
        float2 t = float2(1.0 / w, 1.0 / h);
        float3 a = src.sample(s, in.uv + float2(-t.x, -t.y)).rgb;
        float3 b = src.sample(s, in.uv + float2( t.x, -t.y)).rgb;
        float3 c = src.sample(s, in.uv + float2(-t.x,  t.y)).rgb;
        float3 d = src.sample(s, in.uv + float2( t.x,  t.y)).rgb;
        return float4((a + b + c + d) * 0.25, 1.0);
    }
)";

constexpr const char* k_upsample_msl = R"(
    #include <metal_stdlib>
    using namespace metal;

    struct VSOut { float4 position [[position]]; float2 uv; };

    vertex VSOut fullscreen_vs(uint vid [[vertex_id]]) {
        float2 uv = float2((vid << 1) & 2, vid & 2);
        VSOut o;
        o.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
        o.uv       = float2(uv.x, 1.0 - uv.y);
        return o;
    }

    // 9-tap tent filter for soft upsampling. Pipeline blends additively so the
    // destination's existing contents are preserved.
    fragment float4 upsample_fs(VSOut in [[stage_in]],
                                  texture2d<float> low [[texture(0)]],
                                  sampler          s   [[sampler(0)]]) {
        float w = float(low.get_width());
        float h = float(low.get_height());
        float2 t = float2(1.0 / w, 1.0 / h);
        float3 acc = low.sample(s, in.uv).rgb * 4.0;
        acc += low.sample(s, in.uv + float2(-t.x, 0.0)).rgb * 2.0;
        acc += low.sample(s, in.uv + float2( t.x, 0.0)).rgb * 2.0;
        acc += low.sample(s, in.uv + float2(0.0, -t.y)).rgb * 2.0;
        acc += low.sample(s, in.uv + float2(0.0,  t.y)).rgb * 2.0;
        acc += low.sample(s, in.uv + float2(-t.x, -t.y)).rgb;
        acc += low.sample(s, in.uv + float2( t.x, -t.y)).rgb;
        acc += low.sample(s, in.uv + float2(-t.x,  t.y)).rgb;
        acc += low.sample(s, in.uv + float2( t.x,  t.y)).rgb;
        return float4(acc / 16.0, 1.0);
    }
)";

constexpr const char* k_tonemap_msl = R"(
    #include <metal_stdlib>
    using namespace metal;

    struct VSOut { float4 position [[position]]; float2 uv; };

    vertex VSOut tonemap_vs(uint vid [[vertex_id]]) {
        float2 uv = float2((vid << 1) & 2, vid & 2);
        VSOut o;
        o.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
        o.uv       = float2(uv.x, 1.0 - uv.y);
        return o;
    }

    // ACES Fitted tonemap (Stephen Hill). Maps HDR linear -> display linear in [0,1].
    float3 aces(float3 x) {
        const float3x3 IN = float3x3(
            0.59719, 0.07600, 0.02840,
            0.35458, 0.90834, 0.13383,
            0.04823, 0.01566, 0.83777);
        const float3x3 OUT = float3x3(
             1.60475, -0.10208, -0.00327,
            -0.53108,  1.10813, -0.07276,
            -0.07367, -0.00605,  1.07602);
        x = IN * x;
        float3 a = x * (x + 0.0245786) - 0.000090537;
        float3 b = x * (0.983729 * x + 0.4329510) + 0.238081;
        x = a / b;
        x = OUT * x;
        return saturate(x);
    }

    fragment float4 tonemap_fs(VSOut in [[stage_in]],
                                 texture2d<float> hdr   [[texture(0)]],
                                 texture2d<float> bloom [[texture(1)]],
                                 sampler          s     [[sampler(0)]]) {
        float3 c = hdr.sample(s, in.uv).rgb;
        float3 b = bloom.sample(s, in.uv).rgb;
        c += b * 0.04;  // bloom intensity (subtle)
        return float4(aces(c), 1.0);
    }
)";

// ---------- Scene ----------

struct Sphere {
    mge::math::Vec3 position;
    mge::math::Vec3 albedo;
    float           metallic;
    float           roughness;
    float           ao;
};

const std::array<Sphere, 5> k_spheres = {{
    {{-2.4f, 1.2f, 0}, {0.722f, 0.451f, 0.20f}, 0.0f, 0.10f, 1.0f},
    {{-1.2f, 1.2f, 0}, {0.722f, 0.451f, 0.20f}, 0.0f, 0.50f, 1.0f},
    {{ 0.0f, 1.2f, 0}, {0.722f, 0.451f, 0.20f}, 0.0f, 1.00f, 1.0f},
    {{ 1.2f, 1.2f, 0}, {0.95f,  0.64f,  0.54f}, 1.0f, 0.10f, 1.0f},
    {{ 2.4f, 1.2f, 0}, {0.95f,  0.64f,  0.54f}, 1.0f, 0.50f, 1.0f},
}};

constexpr std::uint32_t k_shadow_size = 2048;

struct CubeProto {
    mge::math::Vec3 center;
    mge::math::Vec3 albedo;
    float           metallic;
    float           roughness;
};

std::vector<CubeProto> build_cube_field(std::uint32_t side) {
    std::vector<CubeProto> out;
    out.reserve(static_cast<std::size_t>(side) * side);
    const float spacing = 1.5f;
    const float origin  = -(static_cast<float>(side) - 1.0f) * 0.5f * spacing;
    for (std::uint32_t y = 0; y < side; ++y) {
        for (std::uint32_t x = 0; x < side; ++x) {
            CubeProto c;
            c.center = mge::math::Vec3{
                origin + static_cast<float>(x) * spacing,
                0.25f,
                origin + static_cast<float>(y) * spacing,
            };
            // Skip cubes very near the spheres so the foreground stays clear.
            if (std::abs(c.center.x) < 4.0f && std::abs(c.center.z) < 1.5f) continue;

            const float hue   = static_cast<float>((x * 13u + y * 7u) % 60u) / 60.0f;
            c.albedo   = mge::math::Vec3{0.25f + 0.5f * hue, 0.35f + 0.4f * (1 - hue), 0.55f};
            c.metallic = (x + y) % 5u == 0 ? 1.0f : 0.0f;
            c.roughness = 0.2f + 0.6f * static_cast<float>((x * 5u + y) % 7u) / 7.0f;
            out.push_back(c);
        }
    }
    return out;
}

constexpr float k_cube_half = 0.25f;  // world half-extent after scaling

mge::math::Aabb cube_world_aabb(const CubeProto& c) noexcept {
    return mge::math::Aabb::from_points(
        mge::math::Vec3{c.center.x - k_cube_half, c.center.y - k_cube_half, c.center.z - k_cube_half},
        mge::math::Vec3{c.center.x + k_cube_half, c.center.y + k_cube_half, c.center.z + k_cube_half});
}

struct DeferredRenderer {
    std::unique_ptr<mge::rhi::Device>         device;
    std::unique_ptr<mge::rhi::Queue>          queue;

    std::unique_ptr<mge::rhi::Buffer> sphere_vbuf;
    std::unique_ptr<mge::rhi::Buffer> sphere_ibuf;
    std::uint32_t                     sphere_index_count = 0;
    std::unique_ptr<mge::rhi::Buffer> ground_vbuf;
    std::unique_ptr<mge::rhi::Buffer> ground_ibuf;
    std::uint32_t                     ground_index_count = 0;
    std::unique_ptr<mge::rhi::Buffer> cube_vbuf;
    std::unique_ptr<mge::rhi::Buffer> cube_ibuf;
    std::uint32_t                     cube_index_count = 0;

    std::unique_ptr<mge::rhi::Shader>         shadow_shader;
    std::unique_ptr<mge::rhi::RenderPipeline> shadow_pso;
    std::unique_ptr<mge::rhi::Shader>         gbuffer_shader;
    std::unique_ptr<mge::rhi::RenderPipeline> gbuffer_pso;
    std::unique_ptr<mge::rhi::Shader>         lighting_shader;
    std::unique_ptr<mge::rhi::RenderPipeline> lighting_pso;
    std::unique_ptr<mge::rhi::Shader>         tonemap_shader;
    std::unique_ptr<mge::rhi::RenderPipeline> tonemap_pso;
    std::unique_ptr<mge::rhi::Shader>         bright_shader;
    std::unique_ptr<mge::rhi::RenderPipeline> bright_pso;
    std::unique_ptr<mge::rhi::Shader>         downsample_shader;
    std::unique_ptr<mge::rhi::RenderPipeline> downsample_pso;
    std::unique_ptr<mge::rhi::Shader>         upsample_shader;
    std::unique_ptr<mge::rhi::RenderPipeline> upsample_pso;

    std::unique_ptr<mge::rhi::Buffer>  instance_buf;
    std::size_t                        instance_capacity = 0;
    std::unique_ptr<mge::rhi::Buffer>  frame_buf;
    std::unique_ptr<mge::rhi::Buffer>  lighting_buf;
    std::unique_ptr<mge::rhi::Sampler> linear_clamp;
    std::unique_ptr<mge::rhi::Sampler> shadow_sampler;

    static std::unique_ptr<DeferredRenderer> create(mge::rhi::PixelFormat backbuffer_fmt,
                                                     std::size_t instance_cap) {
        using namespace mge::rhi;
        auto r = std::make_unique<DeferredRenderer>();
        r->device = Device::create();
        if (!r->device) return nullptr;
        r->queue           = r->device->create_queue("deferred.queue");
        r->shadow_shader     = r->device->create_shader_from_msl({k_shadow_msl,     "shadow"});
        r->gbuffer_shader    = r->device->create_shader_from_msl({k_gbuffer_msl,    "gbuffer"});
        r->lighting_shader   = r->device->create_shader_from_msl({k_lighting_msl,   "lighting"});
        r->tonemap_shader    = r->device->create_shader_from_msl({k_tonemap_msl,    "tonemap"});
        r->bright_shader     = r->device->create_shader_from_msl({k_bright_msl,     "bright"});
        r->downsample_shader = r->device->create_shader_from_msl({k_downsample_msl, "downsample"});
        r->upsample_shader   = r->device->create_shader_from_msl({k_upsample_msl,   "upsample"});
        if (!r->queue || !r->shadow_shader || !r->gbuffer_shader ||
            !r->lighting_shader || !r->tonemap_shader ||
            !r->bright_shader || !r->downsample_shader || !r->upsample_shader) {
            return nullptr;
        }

        auto upload = [&](const void* data, std::size_t size, BufferUsage usage,
                          const char* label) {
            BufferDesc d;
            d.size              = size;
            d.usage             = usage;
            d.storage           = StorageMode::Shared;
            d.initial_data      = data;
            d.initial_data_size = size;
            d.label             = label;
            return r->device->create_buffer(d);
        };

        const auto sphere = mge::assets::make_sphere_pbr(20, 32);
        r->sphere_index_count = static_cast<std::uint32_t>(sphere.indices.size());
        r->sphere_vbuf = upload(sphere.vertices.data(),
                                 sphere.vertices.size() * sizeof(mge::assets::PbrVertex),
                                 BufferUsage::Vertex, "sphere.vbuf");
        r->sphere_ibuf = upload(sphere.indices.data(),
                                 sphere.indices.size() * sizeof(std::uint32_t),
                                 BufferUsage::Index, "sphere.ibuf");

        const auto ground = mge::assets::make_ground_plane_pbr(30.0f);
        r->ground_index_count = static_cast<std::uint32_t>(ground.indices.size());
        r->ground_vbuf = upload(ground.vertices.data(),
                                 ground.vertices.size() * sizeof(mge::assets::PbrVertex),
                                 BufferUsage::Vertex, "ground.vbuf");
        r->ground_ibuf = upload(ground.indices.data(),
                                 ground.indices.size() * sizeof(std::uint32_t),
                                 BufferUsage::Index, "ground.ibuf");

        const auto cube = mge::assets::make_cube_pbr();
        r->cube_index_count = static_cast<std::uint32_t>(cube.indices.size());
        r->cube_vbuf = upload(cube.vertices.data(),
                               cube.vertices.size() * sizeof(mge::assets::PbrVertex),
                               BufferUsage::Vertex, "cube.vbuf");
        r->cube_ibuf = upload(cube.indices.data(),
                               cube.indices.size() * sizeof(std::uint32_t),
                               BufferUsage::Index, "cube.ibuf");

        BufferDesc ib;
        ib.size    = sizeof(InstanceData) * instance_cap;
        ib.usage   = BufferUsage::Uniform | BufferUsage::Storage;
        ib.storage = StorageMode::Shared;
        ib.label   = "instances";
        r->instance_buf      = r->device->create_buffer(ib);
        r->instance_capacity = instance_cap;

        BufferDesc fb;
        fb.size    = sizeof(FrameConstants);
        fb.usage   = BufferUsage::Uniform;
        fb.storage = StorageMode::Shared;
        fb.label   = "frame";
        r->frame_buf = r->device->create_buffer(fb);

        BufferDesc lb;
        lb.size    = sizeof(LightingConstants);
        lb.usage   = BufferUsage::Uniform;
        lb.storage = StorageMode::Shared;
        lb.label   = "lighting";
        r->lighting_buf = r->device->create_buffer(lb);

        SamplerDesc sd;
        sd.min_filter = FilterMode::Linear;
        sd.mag_filter = FilterMode::Linear;
        sd.address_u  = AddressMode::ClampToEdge;
        sd.address_v  = AddressMode::ClampToEdge;
        sd.label      = "linear.clamp";
        r->linear_clamp = r->device->create_sampler(sd);

        SamplerDesc shd;
        shd.min_filter = FilterMode::Nearest;
        shd.mag_filter = FilterMode::Nearest;
        shd.address_u  = AddressMode::ClampToEdge;
        shd.address_v  = AddressMode::ClampToEdge;
        shd.label      = "shadow";
        r->shadow_sampler = r->device->create_sampler(shd);

        if (!r->sphere_vbuf || !r->sphere_ibuf || !r->ground_vbuf || !r->ground_ibuf ||
            !r->cube_vbuf || !r->cube_ibuf || !r->instance_buf || !r->frame_buf ||
            !r->lighting_buf || !r->linear_clamp || !r->shadow_sampler) return nullptr;

        const VertexLayout layout{
            {VertexBufferLayout{sizeof(mge::assets::PbrVertex), false}},
            {
                VertexAttribute{0, VertexFormat::Float32x3, offsetof(mge::assets::PbrVertex, position), 0},
                VertexAttribute{1, VertexFormat::Float32x3, offsetof(mge::assets::PbrVertex, normal),   0},
            }};

        {
            RenderPipelineDesc pd;
            pd.vertex_shader        = r->shadow_shader.get();
            pd.fragment_shader      = nullptr;
            pd.vertex_entry         = "shadow_vs";
            pd.vertex_layout        = layout;
            pd.topology             = PrimitiveTopology::TriangleList;
            pd.num_color_targets    = 0;
            pd.depth.format         = PixelFormat::Depth32Float;
            pd.depth.write_enabled  = true;
            pd.depth.compare        = DepthCompare::Less;
            pd.rasterizer.cull_mode = CullMode::Front;
            pd.rasterizer.front_face = FrontFace::CounterClockwise;
            pd.label                 = "shadow.pso";
            r->shadow_pso            = r->device->create_render_pipeline(pd);
        }

        {
            RenderPipelineDesc pd;
            pd.vertex_shader   = r->gbuffer_shader.get();
            pd.fragment_shader = r->gbuffer_shader.get();
            pd.vertex_entry    = "gbuffer_vs";
            pd.fragment_entry  = "gbuffer_fs";
            pd.vertex_layout   = layout;
            pd.topology        = PrimitiveTopology::TriangleList;
            pd.color_targets[0].format = PixelFormat::RGBA8Unorm;
            pd.color_targets[1].format = PixelFormat::RGBA16Float;
            pd.num_color_targets       = 2;
            pd.depth.format            = PixelFormat::Depth32Float;
            pd.depth.write_enabled     = true;
            pd.depth.compare           = DepthCompare::Less;
            pd.rasterizer.cull_mode    = CullMode::Back;
            pd.rasterizer.front_face   = FrontFace::CounterClockwise;
            pd.label                   = "gbuffer.pso";
            r->gbuffer_pso             = r->device->create_render_pipeline(pd);
        }

        {
            RenderPipelineDesc pd;
            pd.vertex_shader           = r->lighting_shader.get();
            pd.fragment_shader         = r->lighting_shader.get();
            pd.vertex_entry            = "lighting_vs";
            pd.fragment_entry          = "lighting_fs";
            pd.topology                = PrimitiveTopology::TriangleList;
            pd.color_targets[0].format = PixelFormat::RGBA16Float;
            pd.num_color_targets       = 1;
            pd.rasterizer.cull_mode    = CullMode::None;
            pd.label                   = "lighting.pso";
            r->lighting_pso            = r->device->create_render_pipeline(pd);
        }

        {
            RenderPipelineDesc pd;
            pd.vertex_shader           = r->tonemap_shader.get();
            pd.fragment_shader         = r->tonemap_shader.get();
            pd.vertex_entry            = "tonemap_vs";
            pd.fragment_entry          = "tonemap_fs";
            pd.topology                = PrimitiveTopology::TriangleList;
            pd.color_targets[0].format = backbuffer_fmt;
            pd.num_color_targets       = 1;
            pd.rasterizer.cull_mode    = CullMode::None;
            pd.label                   = "tonemap.pso";
            r->tonemap_pso             = r->device->create_render_pipeline(pd);
        }

        // Bright pass: read HDR, threshold, write bloom_mip0.
        {
            RenderPipelineDesc pd;
            pd.vertex_shader           = r->bright_shader.get();
            pd.fragment_shader         = r->bright_shader.get();
            pd.vertex_entry            = "fullscreen_vs";
            pd.fragment_entry          = "bright_fs";
            pd.topology                = PrimitiveTopology::TriangleList;
            pd.color_targets[0].format = PixelFormat::RGBA16Float;
            pd.num_color_targets       = 1;
            pd.rasterizer.cull_mode    = CullMode::None;
            pd.label                   = "bright.pso";
            r->bright_pso              = r->device->create_render_pipeline(pd);
        }

        // Downsample pass: shared by all 4 mip steps (4-tap bilinear box).
        {
            RenderPipelineDesc pd;
            pd.vertex_shader           = r->downsample_shader.get();
            pd.fragment_shader         = r->downsample_shader.get();
            pd.vertex_entry            = "fullscreen_vs";
            pd.fragment_entry          = "downsample_fs";
            pd.topology                = PrimitiveTopology::TriangleList;
            pd.color_targets[0].format = PixelFormat::RGBA16Float;
            pd.num_color_targets       = 1;
            pd.rasterizer.cull_mode    = CullMode::None;
            pd.label                   = "downsample.pso";
            r->downsample_pso          = r->device->create_render_pipeline(pd);
        }

        // Upsample pass: ADDITIVE blend (src=One, dst=One). Combines its output
        // with the destination's existing higher-res mip via LoadAction::Load.
        {
            RenderPipelineDesc pd;
            pd.vertex_shader           = r->upsample_shader.get();
            pd.fragment_shader         = r->upsample_shader.get();
            pd.vertex_entry            = "fullscreen_vs";
            pd.fragment_entry          = "upsample_fs";
            pd.topology                = PrimitiveTopology::TriangleList;
            pd.color_targets[0].format    = PixelFormat::RGBA16Float;
            pd.color_targets[0].blend     = true;
            pd.color_targets[0].src_color = BlendFactor::One;
            pd.color_targets[0].dst_color = BlendFactor::One;
            pd.color_targets[0].color_op  = BlendOp::Add;
            pd.color_targets[0].src_alpha = BlendFactor::One;
            pd.color_targets[0].dst_alpha = BlendFactor::One;
            pd.color_targets[0].alpha_op  = BlendOp::Add;
            pd.num_color_targets          = 1;
            pd.rasterizer.cull_mode       = CullMode::None;
            pd.label                      = "upsample.pso";
            r->upsample_pso               = r->device->create_render_pipeline(pd);
        }

        if (!r->shadow_pso || !r->gbuffer_pso || !r->lighting_pso || !r->tonemap_pso ||
            !r->bright_pso || !r->downsample_pso || !r->upsample_pso) {
            return nullptr;
        }
        return r;
    }
};

mge::math::Mat4 compute_light_view_proj(mge::math::Vec3 sun_dir_ws) {
    using namespace mge::math;
    const Vec3  to_light = Vec3{-sun_dir_ws.x, -sun_dir_ws.y, -sun_dir_ws.z};
    const float D        = 50.0f;
    const Vec3  light_pos{to_light.x * D, to_light.y * D, to_light.z * D};
    const Mat4  view     = look_at_rh(light_pos, Vec3{0, 0, 0}, Vec3{0, 1, 0});
    const Mat4  proj     = orthographic_rh_zo(-30.0f, 30.0f, -30.0f, 30.0f, 0.1f, 120.0f);
    return proj * view;
}

void fill_lighting_constants(DeferredRenderer& r, const mge::scene::Camera& cam,
                              const mge::math::Mat4& light_vp,
                              const mge::math::Vec3& sun_dir) {
    LightingConstants u{};
    u.view_proj_inv   = mge::math::inverse(cam.view_projection());
    u.light_view_proj = light_vp;
    u.camera_ws[0]    = cam.eye().x;
    u.camera_ws[1]    = cam.eye().y;
    u.camera_ws[2]    = cam.eye().z;
    u.camera_ws[3]    = 1.0f;
    u.sun_dir_ws[0]   = sun_dir.x;
    u.sun_dir_ws[1]   = sun_dir.y;
    u.sun_dir_ws[2]   = sun_dir.z;
    u.sun_dir_ws[3]   = 1.0f;
    u.sun_color[0]    = 3.2f;
    u.sun_color[1]    = 3.0f;
    u.sun_color[2]    = 2.6f;
    u.sun_color[3]    = 1.0f;
    u.ambient[0]      = 0.12f;
    u.ambient[1]      = 0.14f;
    u.ambient[2]      = 0.18f;
    u.ambient[3]      = 0.0f;
    u.shadow_params[0] = 1.0f / static_cast<float>(k_shadow_size);
    u.shadow_params[1] = 0.0015f;
    u.shadow_params[2] = static_cast<float>(k_shadow_size);
    u.shadow_params[3] = 0.0f;
    std::memcpy(r.lighting_buf->contents(), &u, sizeof(u));
}

int run_headless(std::size_t instance_cap) {
    auto r = DeferredRenderer::create(mge::rhi::PixelFormat::BGRA8UnormSrgb, instance_cap);
    if (!r) { std::fprintf(stderr, "headless: init failed\n"); return 1; }
    const auto info = r->device->info();
    std::printf("[hello_metal] headless smoke ok: device=%s, sphere %u idx, cube %u idx\n",
                info.name.c_str(), r->sphere_index_count, r->cube_index_count);
    return 0;
}

int run_windowed(const Args& a) {
    using namespace mge::platform;
    using namespace mge::rhi;
    using mge::frame_graph::FrameGraph;
    using mge::frame_graph::PassBuilder;
    using mge::frame_graph::RenderContext;
    using mge::frame_graph::TransientTextureDesc;

    auto& app = App::get();
    app.set_name("hello_metal");

    WindowDesc wd;
    wd.title  = "MetalGameEngine - hello_metal (PBR + shadow + instancing)";
    wd.width  = a.width;
    wd.height = a.height;
    Window window(wd);

    const auto cubes_proto = build_cube_field(a.cube_side);
    const std::size_t instance_cap = k_spheres.size() + 1 + cubes_proto.size() + 8;
    std::printf("[hello_metal] cube field: %ux%u -> %zu cubes (after empty-center skip)\n",
                a.cube_side, a.cube_side, cubes_proto.size());

    constexpr PixelFormat backbuffer_fmt = PixelFormat::BGRA8UnormSrgb;
    auto r = DeferredRenderer::create(backbuffer_fmt, instance_cap);
    if (!r) { std::fprintf(stderr, "renderer init failed\n"); return 1; }
    const auto info = r->device->info();
    std::printf("[hello_metal] device: %s, instance_cap=%zu\n",
                info.name.c_str(), instance_cap);

    auto swap = r->device->create_swapchain(window.native_layer(), backbuffer_fmt);
    if (!swap) { std::fprintf(stderr, "swapchain init failed\n"); return 1; }

    auto sync_drawable_size = [&]() {
        const std::uint32_t w = window.drawable_width();
        const std::uint32_t h = window.drawable_height();
        if (w && h) swap->resize(w, h);
    };
    sync_drawable_size();

    mge::scene::Camera camera;
    camera.set_perspective(mge::math::radians(50.0f),
                           static_cast<float>(window.drawable_width()) /
                               static_cast<float>(window.drawable_height()),
                           0.1f, 200.0f);
    camera.look_at({0.0f, 3.0f, 7.0f}, {0, 1.2f, 0}, {0, 1, 0});

    FrameGraph fg(*r->device);

    const mge::math::Vec3 sun_dir = mge::math::normalize(mge::math::Vec3{-0.6f, -1.0f, -0.4f});
    const mge::math::Mat4 light_vp = compute_light_view_proj(sun_dir);

    std::vector<std::uint32_t> visible_cubes;
    visible_cubes.reserve(cubes_proto.size());

    mge::core::FrameStats stats;
    auto                  prev   = mge::core::now();
    auto                  origin = mge::core::now();
    int                   frame  = 0;

    while (!window.should_close()) {
        app.poll_events();

        if (window.consume_resize_event()) {
            sync_drawable_size();
            camera.set_aspect(static_cast<float>(window.drawable_width()) /
                              static_cast<float>(window.drawable_height()));
        }

        auto frame_drawable = swap->acquire_frame();
        if (!frame_drawable.valid()) continue;

        const float t       = static_cast<float>(mge::core::seconds(mge::core::now() - origin));
        const mge::math::Mat4 wobble = mge::math::rotation_y(t * 0.4f);

        // Build the per-frame instance buffer.
        auto* instances = static_cast<InstanceData*>(r->instance_buf->contents());
        // Spheres at [0..5)
        for (std::size_t i = 0; i < k_spheres.size(); ++i) {
            const auto&     s     = k_spheres[i];
            const mge::math::Mat4 m = wobble * mge::math::translation(s.position);
            InstanceData    inst{};
            inst.model       = m;
            inst.model_inv_t = mge::math::transpose(mge::math::inverse(m));
            inst.albedo_ao[0] = s.albedo.x; inst.albedo_ao[1] = s.albedo.y;
            inst.albedo_ao[2] = s.albedo.z; inst.albedo_ao[3] = s.ao;
            inst.mr[0]        = s.metallic; inst.mr[1] = s.roughness;
            inst.mr[2]        = 0.0f;       inst.mr[3] = 0.0f;
            instances[i] = inst;
        }
        // Ground at [5]
        {
            InstanceData inst{};
            inst.model           = mge::math::Mat4::identity();
            inst.model_inv_t     = mge::math::Mat4::identity();
            inst.albedo_ao[0]    = 0.35f; inst.albedo_ao[1] = 0.35f;
            inst.albedo_ao[2]    = 0.35f; inst.albedo_ao[3] = 1.0f;
            inst.mr[0]           = 0.0f;  inst.mr[1] = 0.85f;
            inst.mr[2]           = 0.0f;  inst.mr[3] = 0.0f;
            instances[k_spheres.size()] = inst;
        }

        // Frustum-cull the cube field against the camera.
        const mge::math::Frustum cam_frustum =
            mge::math::Frustum::from_view_projection(camera.view_projection());
        visible_cubes.clear();
        for (std::uint32_t i = 0; i < cubes_proto.size(); ++i) {
            if (mge::math::aabb_visible(cam_frustum, cube_world_aabb(cubes_proto[i]))) {
                visible_cubes.push_back(i);
            }
        }
        const std::uint32_t cube_base = static_cast<std::uint32_t>(k_spheres.size() + 1);
        for (std::size_t i = 0; i < visible_cubes.size(); ++i) {
            const auto& c = cubes_proto[visible_cubes[i]];
            InstanceData inst{};
            const mge::math::Mat4 scale_m = mge::math::scale({k_cube_half, k_cube_half, k_cube_half});
            inst.model        = mge::math::translation(c.center) * scale_m;
            inst.model_inv_t  = mge::math::transpose(mge::math::inverse(inst.model));
            inst.albedo_ao[0] = c.albedo.x; inst.albedo_ao[1] = c.albedo.y;
            inst.albedo_ao[2] = c.albedo.z; inst.albedo_ao[3] = 1.0f;
            inst.mr[0]        = c.metallic; inst.mr[1] = c.roughness;
            inst.mr[2]        = 0.0f;       inst.mr[3] = 0.0f;
            instances[cube_base + i] = inst;
        }

        FrameConstants fc{};
        fc.view_proj       = camera.view_projection();
        fc.light_view_proj = light_vp;
        std::memcpy(r->frame_buf->contents(), &fc, sizeof(fc));

        fill_lighting_constants(*r, camera, light_vp, sun_dir);

        const std::uint32_t cube_visible_count = static_cast<std::uint32_t>(visible_cubes.size());

        fg.reset();
        const std::uint32_t fw = frame_drawable.texture()->width();
        const std::uint32_t fh = frame_drawable.texture()->height();

        auto bb = fg.import_texture(*frame_drawable.texture(), "backbuffer");

        TransientTextureDesc gb0_desc{
            fw, fh, 1, PixelFormat::RGBA8Unorm,
            TextureUsage::RenderTarget | TextureUsage::ShaderRead, StorageMode::Private};
        TransientTextureDesc gb1_desc = gb0_desc;
        gb1_desc.format               = PixelFormat::RGBA16Float;
        TransientTextureDesc depth_desc{
            fw, fh, 1, PixelFormat::Depth32Float,
            TextureUsage::RenderTarget | TextureUsage::ShaderRead, StorageMode::Private};
        TransientTextureDesc hdr_desc = gb1_desc;
        TransientTextureDesc shadow_desc{
            k_shadow_size, k_shadow_size, 1, PixelFormat::Depth32Float,
            TextureUsage::RenderTarget | TextureUsage::ShaderRead, StorageMode::Private};

        auto shadow_map = fg.create_texture(shadow_desc, "shadow_map");
        auto gb0        = fg.create_texture(gb0_desc,    "gb0");
        auto gb1        = fg.create_texture(gb1_desc,    "gb1");
        auto depth      = fg.create_texture(depth_desc,  "depth");
        auto hdr        = fg.create_texture(hdr_desc,    "hdr");

        fg.add_pass("shadow",
            [&](PassBuilder& pb) {
                pb.write_depth(shadow_map, LoadAction::Clear, 1.0f);
            },
            [&](RenderContext& ctx) {
                auto rp = ctx.make_render_pass_desc();
                RenderEncoder enc = ctx.cmd().begin_render_pass(rp);
                enc.set_pipeline(*r->shadow_pso);
                enc.set_vertex_buffer(*r->frame_buf, 2);

                enc.set_vertex_buffer(*r->sphere_vbuf, 0);
                enc.set_vertex_buffer(*r->instance_buf, 1, 0);
                enc.draw_indexed(r->sphere_index_count, IndexType::UInt32,
                                  *r->sphere_ibuf, 0, static_cast<std::uint32_t>(k_spheres.size()));

                enc.set_vertex_buffer(*r->ground_vbuf, 0);
                enc.set_vertex_buffer(*r->instance_buf, 1, k_spheres.size() * sizeof(InstanceData));
                enc.draw_indexed(r->ground_index_count, IndexType::UInt32,
                                  *r->ground_ibuf, 0, 1);

                if (cube_visible_count > 0) {
                    enc.set_vertex_buffer(*r->cube_vbuf, 0);
                    enc.set_vertex_buffer(*r->instance_buf, 1,
                                            cube_base * sizeof(InstanceData));
                    enc.draw_indexed(r->cube_index_count, IndexType::UInt32,
                                      *r->cube_ibuf, 0, cube_visible_count);
                }
            });

        fg.add_pass("gbuffer",
            [&](PassBuilder& pb) {
                pb.write_color(gb0,   LoadAction::Clear, 0, 0, 0, 0);
                pb.write_color(gb1,   LoadAction::Clear, 0, 0, 0, 0);
                pb.write_depth(depth, LoadAction::Clear, 1.0f);
            },
            [&](RenderContext& ctx) {
                auto rp = ctx.make_render_pass_desc();
                RenderEncoder enc = ctx.cmd().begin_render_pass(rp);
                enc.set_pipeline(*r->gbuffer_pso);
                enc.set_vertex_buffer(*r->frame_buf, 2);

                enc.set_vertex_buffer(*r->sphere_vbuf, 0);
                enc.set_vertex_buffer(*r->instance_buf,   1, 0);
                enc.set_fragment_buffer(*r->instance_buf, 0, 0);
                enc.draw_indexed(r->sphere_index_count, IndexType::UInt32,
                                  *r->sphere_ibuf, 0, static_cast<std::uint32_t>(k_spheres.size()));

                const std::size_t ground_off = k_spheres.size() * sizeof(InstanceData);
                enc.set_vertex_buffer(*r->ground_vbuf, 0);
                enc.set_vertex_buffer(*r->instance_buf,   1, ground_off);
                enc.set_fragment_buffer(*r->instance_buf, 0, ground_off);
                enc.draw_indexed(r->ground_index_count, IndexType::UInt32,
                                  *r->ground_ibuf, 0, 1);

                if (cube_visible_count > 0) {
                    const std::size_t off = cube_base * sizeof(InstanceData);
                    enc.set_vertex_buffer(*r->cube_vbuf, 0);
                    enc.set_vertex_buffer(*r->instance_buf,   1, off);
                    enc.set_fragment_buffer(*r->instance_buf, 0, off);
                    enc.draw_indexed(r->cube_index_count, IndexType::UInt32,
                                      *r->cube_ibuf, 0, cube_visible_count);
                }
            });

        fg.add_pass("lighting",
            [&](PassBuilder& pb) {
                pb.read(gb0,        mge::frame_graph::ResourceUsage::ShaderRead);
                pb.read(gb1,        mge::frame_graph::ResourceUsage::ShaderRead);
                pb.read(depth,      mge::frame_graph::ResourceUsage::ShaderRead);
                pb.read(shadow_map, mge::frame_graph::ResourceUsage::ShaderRead);
                pb.write_color(hdr, LoadAction::Clear, 0, 0, 0, 1);
            },
            [&](RenderContext& ctx) {
                auto rp = ctx.make_render_pass_desc();
                RenderEncoder enc = ctx.cmd().begin_render_pass(rp);
                enc.set_pipeline(*r->lighting_pso);
                enc.set_fragment_texture(ctx.texture(gb0),        0);
                enc.set_fragment_texture(ctx.texture(gb1),        1);
                enc.set_fragment_texture(ctx.texture(depth),      2);
                enc.set_fragment_texture(ctx.texture(shadow_map), 3);
                enc.set_fragment_sampler(*r->linear_clamp,        0);
                enc.set_fragment_sampler(*r->shadow_sampler,      1);
                enc.set_fragment_buffer(*r->lighting_buf, 0);
                enc.draw(3);
            });

        // ---- Bloom (5-mip down/up pyramid, additive in upsample) ----
        constexpr int kBloomMips = 5;
        auto mip_dim = [&](int level) {
            const std::uint32_t w = std::max(1u, fw >> (level + 1));
            const std::uint32_t h = std::max(1u, fh >> (level + 1));
            return std::pair<std::uint32_t, std::uint32_t>{w, h};
        };

        mge::frame_graph::TextureHandle bloom[kBloomMips];
        for (int i = 0; i < kBloomMips; ++i) {
            const auto [w, h] = mip_dim(i);
            TransientTextureDesc bd{
                w, h, 1, PixelFormat::RGBA16Float,
                TextureUsage::RenderTarget | TextureUsage::ShaderRead, StorageMode::Private};
            bloom[i] = fg.create_texture(bd, "bloom");
        }

        // Bright pass: HDR -> bloom[0].
        fg.add_pass("bloom.bright",
            [&](PassBuilder& pb) {
                pb.read(hdr, mge::frame_graph::ResourceUsage::ShaderRead);
                pb.write_color(bloom[0], LoadAction::Clear, 0, 0, 0, 1);
            },
            [&](RenderContext& ctx) {
                auto rp = ctx.make_render_pass_desc();
                RenderEncoder enc = ctx.cmd().begin_render_pass(rp);
                enc.set_pipeline(*r->bright_pso);
                enc.set_fragment_texture(ctx.texture(hdr), 0);
                enc.set_fragment_sampler(*r->linear_clamp, 0);
                enc.draw(3);
            });

        // Downsample chain: bloom[i] -> bloom[i+1].
        for (int i = 0; i + 1 < kBloomMips; ++i) {
            const auto src = bloom[i];
            const auto dst = bloom[i + 1];
            fg.add_pass("bloom.ds",
                [&, src, dst](PassBuilder& pb) {
                    pb.read(src, mge::frame_graph::ResourceUsage::ShaderRead);
                    pb.write_color(dst, LoadAction::Clear, 0, 0, 0, 1);
                },
                [&, src](RenderContext& ctx) {
                    auto rp = ctx.make_render_pass_desc();
                    RenderEncoder enc = ctx.cmd().begin_render_pass(rp);
                    enc.set_pipeline(*r->downsample_pso);
                    enc.set_fragment_texture(ctx.texture(src), 0);
                    enc.set_fragment_sampler(*r->linear_clamp, 0);
                    enc.draw(3);
                });
        }

        // Upsample chain: bloom[i+1] -> bloom[i] with additive blend (LoadAction::Load).
        for (int i = kBloomMips - 2; i >= 0; --i) {
            const auto src = bloom[i + 1];
            const auto dst = bloom[i];
            fg.add_pass("bloom.us",
                [&, src, dst](PassBuilder& pb) {
                    pb.read(src, mge::frame_graph::ResourceUsage::ShaderRead);
                    pb.write_color(dst, LoadAction::Load, 0, 0, 0, 1);
                },
                [&, src](RenderContext& ctx) {
                    auto rp = ctx.make_render_pass_desc();
                    RenderEncoder enc = ctx.cmd().begin_render_pass(rp);
                    enc.set_pipeline(*r->upsample_pso);
                    enc.set_fragment_texture(ctx.texture(src), 0);
                    enc.set_fragment_sampler(*r->linear_clamp, 0);
                    enc.draw(3);
                });
        }

        // Tonemap: HDR + bloom[0] -> backbuffer with ACES.
        fg.add_pass("tonemap",
            [&](PassBuilder& pb) {
                pb.read(hdr,      mge::frame_graph::ResourceUsage::ShaderRead);
                pb.read(bloom[0], mge::frame_graph::ResourceUsage::ShaderRead);
                pb.write_color(bb, LoadAction::Clear, 0, 0, 0, 1);
            },
            [&](RenderContext& ctx) {
                auto rp = ctx.make_render_pass_desc();
                RenderEncoder enc = ctx.cmd().begin_render_pass(rp);
                enc.set_pipeline(*r->tonemap_pso);
                enc.set_fragment_texture(ctx.texture(hdr),      0);
                enc.set_fragment_texture(ctx.texture(bloom[0]), 1);
                enc.set_fragment_sampler(*r->linear_clamp, 0);
                enc.draw(3);
            });

        if (!fg.compile()) {
            std::fprintf(stderr, "frame graph compile failed\n");
            return 1;
        }
        fg.execute(*r->queue, &frame_drawable);

        const auto now = mge::core::now();
        const auto dt  = now - prev;
        prev           = now;
        stats.push(mge::core::seconds(dt));

        ++frame;
        if (frame % 60 == 0) {
            std::printf("[hello_metal] frame %4d  last=%.2fms  avg=%.2fms  cubes %u/%zu\n",
                        frame,
                        mge::core::milliseconds(dt),
                        stats.avg_seconds() * 1000.0,
                        cube_visible_count, cubes_proto.size());
            std::fflush(stdout);
        }

        if (a.frames > 0 && frame >= a.frames) window.request_close();
    }

    std::printf("[hello_metal] exiting after %d frames, avg=%.2fms\n",
                frame, stats.avg_seconds() * 1000.0);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const auto a = parse_args(argc, argv);
    std::printf("[hello_metal] %s 0.0.1 (build=%s)\n",
                mge::core::engine_name().data(),
                mge::core::engine_build_kind().data());
    if (a.headless) {
        const std::size_t cap = k_spheres.size() + 1 + static_cast<std::size_t>(a.cube_side) * a.cube_side;
        return run_headless(cap);
    }
    return run_windowed(a);
}
