// M7 demo: deferred PBR + single-cascade shadow map. 4-pass FrameGraph:
//
//   1. Shadow   - renders scene depth from sun's POV into a 2048x2048 depth
//                  texture. Vertex-only pipeline (no fragment, no color).
//   2. GBuffer  - same as M6: writes Albedo+AO + OctaNormal+Roughness+Metallic
//                  + Depth, now including the ground plane.
//   3. Lighting - samples G-Buffer + camera depth + shadow map. PCF 3x3 on
//                  the shadow sample attenuates direct lighting. Writes HDR.
//   4. Tonemap  - Reinhard -> sRGB backbuffer.
//
// Cascade splitting is M7.b (deferred). Single cascade covers the scene with
// a fixed ortho frustum.
//
// Args:
//   --frames N    run for N frames then exit
//   --headless    skip window creation
//   --width W
//   --height H

#include "mge/assets/pbr_mesh.h"
#include "mge/core/time.h"
#include "mge/core/version.h"
#include "mge/frame_graph/frame_graph.h"
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

namespace {

struct Args {
    int           frames   = 0;
    bool          headless = false;
    std::uint32_t width    = 1280;
    std::uint32_t height   = 720;
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
        }
    }
    return a;
}

// ---------- Uniform layouts ----------

struct alignas(16) DrawConstants {
    mge::math::Mat4 view_proj;       // 64 B - camera VP (gbuffer pass)
    mge::math::Mat4 model;           // 64 B
    mge::math::Mat4 model_inv_t;     // 64 B
    mge::math::Mat4 light_view_proj; // 64 B - sun's view-projection (shadow pass)
    float           albedo[4];       // 16 B  rgb=albedo, a=ao
    float           mr[4];           // 16 B  r=metallic, g=roughness, ba=pad
};
static_assert(sizeof(DrawConstants) == 288);

constexpr std::size_t k_draw_stride = 320;
static_assert(sizeof(DrawConstants) <= k_draw_stride);

struct alignas(16) LightingConstants {
    mge::math::Mat4 view_proj_inv;     // 64 B
    mge::math::Mat4 light_view_proj;   // 64 B
    float           camera_ws[4];      // 16 B
    float           sun_dir_ws[4];     // 16 B
    float           sun_color[4];      // 16 B
    float           ambient[4];        // 16 B
    float           shadow_params[4];  // 16 B  r=texel_size, g=bias, b=shadow_map_size
};
static_assert(sizeof(LightingConstants) == 208);

// ---------- Inline MSL ----------

constexpr const char* k_shadow_msl = R"(
    #include <metal_stdlib>
    using namespace metal;

    struct DrawConstants {
        float4x4 view_proj;
        float4x4 model;
        float4x4 model_inv_t;
        float4x4 light_view_proj;
        float4   albedo_ao;
        float4   mr;
    };

    struct VSIn  { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]]; };
    struct VSOut { float4 position [[position]]; };

    vertex VSOut shadow_vs(VSIn in [[stage_in]],
                            device const DrawConstants& d [[buffer(1)]]) {
        VSOut o;
        o.position = d.light_view_proj * d.model * float4(in.position, 1.0);
        return o;
    }
)";

constexpr const char* k_gbuffer_msl = R"(
    #include <metal_stdlib>
    using namespace metal;

    struct DrawConstants {
        float4x4 view_proj;
        float4x4 model;
        float4x4 model_inv_t;
        float4x4 light_view_proj;
        float4   albedo_ao;
        float4   mr;
    };

    struct VSIn  { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]]; };
    struct VSOut { float4 position [[position]]; float3 normal_ws; };

    vertex VSOut gbuffer_vs(VSIn in [[stage_in]],
                             device const DrawConstants& d [[buffer(1)]]) {
        VSOut o;
        o.position  = d.view_proj * d.model * float4(in.position, 1.0);
        o.normal_ws = normalize((d.model_inv_t * float4(in.normal, 0.0)).xyz);
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
                                 device const DrawConstants& d [[buffer(0)]]) {
        GBufOut o;
        o.c0 = float4(d.albedo_ao.rgb, d.albedo_ao.a);
        float2 n = octa_encode(normalize(in.normal_ws));
        o.c1 = float4(n.x, n.y, d.mr.g, d.mr.r);
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
        float4   shadow_params; // r=texel_size, g=bias, b=shadow_map_size
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

    // PCF 3x3 shadow sample. Returns 1.0 = fully lit, 0.0 = fully in shadow.
    float sample_shadow(depth2d<float> shadow_map,
                         sampler          shadow_sampler,
                         float3           world_pos,
                         float4x4         light_view_proj,
                         float            texel_size,
                         float            bias) {
        float4 ls   = light_view_proj * float4(world_pos, 1.0);
        float3 lsn  = ls.xyz / ls.w;
        float2 uv   = lsn.xy * 0.5 + 0.5;
        uv.y        = 1.0 - uv.y;  // flip y for sampling
        // Outside cascade footprint -> assume lit.
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

    fragment float4 tonemap_fs(VSOut in [[stage_in]],
                                 texture2d<float> hdr [[texture(0)]],
                                 sampler          s   [[sampler(0)]]) {
        float3 c = hdr.sample(s, in.uv).rgb;
        c = c / (1.0 + c);
        return float4(c, 1.0);
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

constexpr std::size_t k_draw_count = k_spheres.size() + 1;  // +1 for ground

constexpr std::uint32_t k_shadow_size = 2048;

struct DeferredRenderer {
    std::unique_ptr<mge::rhi::Device>         device;
    std::unique_ptr<mge::rhi::Queue>          queue;

    std::unique_ptr<mge::rhi::Buffer> sphere_vbuf;
    std::unique_ptr<mge::rhi::Buffer> sphere_ibuf;
    std::uint32_t                     sphere_index_count = 0;
    std::unique_ptr<mge::rhi::Buffer> ground_vbuf;
    std::unique_ptr<mge::rhi::Buffer> ground_ibuf;
    std::uint32_t                     ground_index_count = 0;

    std::unique_ptr<mge::rhi::Shader>         shadow_shader;
    std::unique_ptr<mge::rhi::RenderPipeline> shadow_pso;
    std::unique_ptr<mge::rhi::Shader>         gbuffer_shader;
    std::unique_ptr<mge::rhi::RenderPipeline> gbuffer_pso;
    std::unique_ptr<mge::rhi::Shader>         lighting_shader;
    std::unique_ptr<mge::rhi::RenderPipeline> lighting_pso;
    std::unique_ptr<mge::rhi::Shader>         tonemap_shader;
    std::unique_ptr<mge::rhi::RenderPipeline> tonemap_pso;

    std::unique_ptr<mge::rhi::Buffer>  draw_buf;
    std::unique_ptr<mge::rhi::Buffer>  lighting_buf;
    std::unique_ptr<mge::rhi::Sampler> linear_clamp;
    std::unique_ptr<mge::rhi::Sampler> shadow_sampler;

    static std::unique_ptr<DeferredRenderer> create(mge::rhi::PixelFormat backbuffer_fmt) {
        using namespace mge::rhi;
        auto r = std::make_unique<DeferredRenderer>();
        r->device = Device::create();
        if (!r->device) return nullptr;
        r->queue           = r->device->create_queue("deferred.queue");
        r->shadow_shader   = r->device->create_shader_from_msl({k_shadow_msl,   "shadow"});
        r->gbuffer_shader  = r->device->create_shader_from_msl({k_gbuffer_msl,  "gbuffer"});
        r->lighting_shader = r->device->create_shader_from_msl({k_lighting_msl, "lighting"});
        r->tonemap_shader  = r->device->create_shader_from_msl({k_tonemap_msl,  "tonemap"});
        if (!r->queue || !r->shadow_shader || !r->gbuffer_shader ||
            !r->lighting_shader || !r->tonemap_shader) return nullptr;

        // Sphere mesh.
        const auto sphere = mge::assets::make_sphere_pbr(20, 32);
        r->sphere_index_count = static_cast<std::uint32_t>(sphere.indices.size());

        BufferDesc vb;
        vb.size              = sphere.vertices.size() * sizeof(mge::assets::PbrVertex);
        vb.usage             = BufferUsage::Vertex;
        vb.storage           = StorageMode::Shared;
        vb.initial_data      = sphere.vertices.data();
        vb.initial_data_size = vb.size;
        vb.label             = "sphere.vbuf";
        r->sphere_vbuf       = r->device->create_buffer(vb);

        BufferDesc ib;
        ib.size              = sphere.indices.size() * sizeof(std::uint32_t);
        ib.usage             = BufferUsage::Index;
        ib.storage           = StorageMode::Shared;
        ib.initial_data      = sphere.indices.data();
        ib.initial_data_size = ib.size;
        ib.label             = "sphere.ibuf";
        r->sphere_ibuf       = r->device->create_buffer(ib);

        // Ground plane mesh.
        const auto ground = mge::assets::make_ground_plane_pbr(10.0f);
        r->ground_index_count = static_cast<std::uint32_t>(ground.indices.size());

        BufferDesc gvb = vb;
        gvb.size              = ground.vertices.size() * sizeof(mge::assets::PbrVertex);
        gvb.initial_data      = ground.vertices.data();
        gvb.initial_data_size = gvb.size;
        gvb.label             = "ground.vbuf";
        r->ground_vbuf        = r->device->create_buffer(gvb);

        BufferDesc gib = ib;
        gib.size              = ground.indices.size() * sizeof(std::uint32_t);
        gib.initial_data      = ground.indices.data();
        gib.initial_data_size = gib.size;
        gib.label             = "ground.ibuf";
        r->ground_ibuf        = r->device->create_buffer(gib);

        // Uniform buffers.
        BufferDesc db;
        db.size    = k_draw_stride * k_draw_count;
        db.usage   = BufferUsage::Uniform;
        db.storage = StorageMode::Shared;
        db.label   = "draw.uniforms";
        r->draw_buf = r->device->create_buffer(db);

        BufferDesc lb;
        lb.size    = sizeof(LightingConstants);
        lb.usage   = BufferUsage::Uniform;
        lb.storage = StorageMode::Shared;
        lb.label   = "lighting.uniforms";
        r->lighting_buf = r->device->create_buffer(lb);

        // Samplers.
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
        shd.label      = "shadow.sampler";
        r->shadow_sampler = r->device->create_sampler(shd);

        if (!r->sphere_vbuf || !r->sphere_ibuf || !r->ground_vbuf || !r->ground_ibuf ||
            !r->draw_buf || !r->lighting_buf || !r->linear_clamp || !r->shadow_sampler) {
            return nullptr;
        }

        // Shadow pipeline: depth-only, no color targets, no fragment shader.
        {
            RenderPipelineDesc pd;
            pd.vertex_shader   = r->shadow_shader.get();
            pd.fragment_shader = nullptr;
            pd.vertex_entry    = "shadow_vs";
            pd.vertex_layout.buffers    = {VertexBufferLayout{sizeof(mge::assets::PbrVertex), false}};
            pd.vertex_layout.attributes = {
                VertexAttribute{0, VertexFormat::Float32x3, offsetof(mge::assets::PbrVertex, position), 0},
                VertexAttribute{1, VertexFormat::Float32x3, offsetof(mge::assets::PbrVertex, normal),   0},
            };
            pd.topology                = PrimitiveTopology::TriangleList;
            pd.num_color_targets       = 0;
            pd.depth.format            = PixelFormat::Depth32Float;
            pd.depth.write_enabled     = true;
            pd.depth.compare           = DepthCompare::Less;
            pd.rasterizer.cull_mode    = CullMode::Front;  // front-face cull reduces self-shadow acne
            pd.rasterizer.front_face   = FrontFace::CounterClockwise;
            pd.label                   = "shadow.pso";
            r->shadow_pso              = r->device->create_render_pipeline(pd);
        }

        // GBuffer pipeline.
        {
            RenderPipelineDesc pd;
            pd.vertex_shader   = r->gbuffer_shader.get();
            pd.fragment_shader = r->gbuffer_shader.get();
            pd.vertex_entry    = "gbuffer_vs";
            pd.fragment_entry  = "gbuffer_fs";
            pd.vertex_layout.buffers    = {VertexBufferLayout{sizeof(mge::assets::PbrVertex), false}};
            pd.vertex_layout.attributes = {
                VertexAttribute{0, VertexFormat::Float32x3, offsetof(mge::assets::PbrVertex, position), 0},
                VertexAttribute{1, VertexFormat::Float32x3, offsetof(mge::assets::PbrVertex, normal),   0},
            };
            pd.topology                = PrimitiveTopology::TriangleList;
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

        // Lighting pipeline.
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

        // Tonemap pipeline.
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

        if (!r->shadow_pso || !r->gbuffer_pso || !r->lighting_pso || !r->tonemap_pso) {
            return nullptr;
        }
        return r;
    }
};

mge::math::Mat4 compute_light_view_proj(mge::math::Vec3 sun_dir_ws) {
    using namespace mge::math;
    // Place a virtual light at distance D along -sun_dir, looking at origin.
    const Vec3  to_light = Vec3{-sun_dir_ws.x, -sun_dir_ws.y, -sun_dir_ws.z};
    const float D        = 30.0f;
    const Vec3  light_pos{to_light.x * D, to_light.y * D, to_light.z * D};
    const Mat4  view     = look_at_rh(light_pos, Vec3{0, 0, 0}, Vec3{0, 1, 0});
    // Fixed ortho frustum that covers the scene (5 spheres + 20x20 ground).
    const Mat4  proj     = orthographic_rh_zo(-15.0f, 15.0f, -15.0f, 15.0f, 0.1f, 60.0f);
    return proj * view;
}

void fill_draw_constants(DeferredRenderer& r, const mge::scene::Camera& cam,
                          const mge::math::Mat4& light_vp, float t) {
    using namespace mge::math;
    const Mat4 vp     = cam.view_projection();
    const Mat4 wobble = rotation_y(t * 0.4f);

    auto* dst = static_cast<std::byte*>(r.draw_buf->contents());
    auto stamp = [&](std::size_t slot, const Mat4& model, Vec3 albedo, float ao,
                      float metallic, float roughness) {
        DrawConstants d{};
        d.view_proj       = vp;
        d.model           = model;
        d.model_inv_t     = transpose(inverse(model));
        d.light_view_proj = light_vp;
        d.albedo[0]       = albedo.x;
        d.albedo[1]       = albedo.y;
        d.albedo[2]       = albedo.z;
        d.albedo[3]       = ao;
        d.mr[0]           = metallic;
        d.mr[1]           = roughness;
        d.mr[2]           = 0.0f;
        d.mr[3]           = 0.0f;
        std::memcpy(dst + slot * k_draw_stride, &d, sizeof(d));
    };

    for (std::size_t i = 0; i < k_spheres.size(); ++i) {
        const auto& s     = k_spheres[i];
        const Mat4  model = wobble * translation(s.position);
        stamp(i, model, s.albedo, s.ao, s.metallic, s.roughness);
    }

    // Ground plane: identity model, neutral grey, fully diffuse.
    stamp(k_spheres.size(), Mat4::identity(),
          Vec3{0.35f, 0.35f, 0.35f}, 1.0f, 0.0f, 0.85f);
}

void fill_lighting_constants(DeferredRenderer& r, const mge::scene::Camera& cam,
                              const mge::math::Mat4& light_vp,
                              const mge::math::Vec3& sun_dir) {
    using namespace mge::math;
    LightingConstants u{};
    u.view_proj_inv   = inverse(cam.view_projection());
    u.light_view_proj = light_vp;
    u.camera_ws[0]    = cam.eye().x;
    u.camera_ws[1]    = cam.eye().y;
    u.camera_ws[2]    = cam.eye().z;
    u.camera_ws[3]    = 1.0f;

    u.sun_dir_ws[0] = sun_dir.x;
    u.sun_dir_ws[1] = sun_dir.y;
    u.sun_dir_ws[2] = sun_dir.z;
    u.sun_dir_ws[3] = 1.0f;

    u.sun_color[0] = 3.2f;
    u.sun_color[1] = 3.0f;
    u.sun_color[2] = 2.6f;
    u.sun_color[3] = 1.0f;

    u.ambient[0] = 0.12f;
    u.ambient[1] = 0.14f;
    u.ambient[2] = 0.18f;
    u.ambient[3] = 0.0f;

    u.shadow_params[0] = 1.0f / static_cast<float>(k_shadow_size);  // texel size
    u.shadow_params[1] = 0.0015f;                                   // depth bias
    u.shadow_params[2] = static_cast<float>(k_shadow_size);
    u.shadow_params[3] = 0.0f;

    std::memcpy(r.lighting_buf->contents(), &u, sizeof(u));
}

int run_headless() {
    auto r = DeferredRenderer::create(mge::rhi::PixelFormat::BGRA8UnormSrgb);
    if (!r) { std::fprintf(stderr, "headless: init failed\n"); return 1; }
    const auto info = r->device->info();
    std::printf("[hello_metal] headless smoke ok: device=%s, sphere %u idx, ground %u idx\n",
                info.name.c_str(), r->sphere_index_count, r->ground_index_count);
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
    wd.title  = "MetalGameEngine - hello_metal (deferred PBR + shadow)";
    wd.width  = a.width;
    wd.height = a.height;
    Window window(wd);

    constexpr PixelFormat backbuffer_fmt = PixelFormat::BGRA8UnormSrgb;
    auto r = DeferredRenderer::create(backbuffer_fmt);
    if (!r) { std::fprintf(stderr, "renderer init failed\n"); return 1; }
    const auto info = r->device->info();
    std::printf("[hello_metal] device: %s (unified=%d ray_tracing=%d)\n",
                info.name.c_str(), info.has_unified_memory ? 1 : 0,
                info.supports_ray_tracing ? 1 : 0);

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
                           0.1f, 100.0f);
    camera.look_at({0.0f, 3.0f, 7.0f}, {0, 1.2f, 0}, {0, 1, 0});

    FrameGraph fg(*r->device);

    const mge::math::Vec3 sun_dir = mge::math::normalize(mge::math::Vec3{-0.6f, -1.0f, -0.4f});
    const mge::math::Mat4 light_vp = compute_light_view_proj(sun_dir);

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

        const float t = static_cast<float>(mge::core::seconds(mge::core::now() - origin));
        fill_draw_constants(*r, camera, light_vp, t);
        fill_lighting_constants(*r, camera, light_vp, sun_dir);

        fg.reset();
        const std::uint32_t fw = frame_drawable.texture()->width();
        const std::uint32_t fh = frame_drawable.texture()->height();

        auto bb = fg.import_texture(*frame_drawable.texture(), "backbuffer");

        TransientTextureDesc gb0_desc;
        gb0_desc.width   = fw;
        gb0_desc.height  = fh;
        gb0_desc.format  = PixelFormat::RGBA8Unorm;
        gb0_desc.usage   = TextureUsage::RenderTarget | TextureUsage::ShaderRead;
        gb0_desc.storage = StorageMode::Private;

        TransientTextureDesc gb1_desc = gb0_desc;
        gb1_desc.format               = PixelFormat::RGBA16Float;

        TransientTextureDesc depth_desc;
        depth_desc.width   = fw;
        depth_desc.height  = fh;
        depth_desc.format  = PixelFormat::Depth32Float;
        depth_desc.usage   = TextureUsage::RenderTarget | TextureUsage::ShaderRead;
        depth_desc.storage = StorageMode::Private;

        TransientTextureDesc hdr_desc = gb1_desc;

        TransientTextureDesc shadow_desc;
        shadow_desc.width   = k_shadow_size;
        shadow_desc.height  = k_shadow_size;
        shadow_desc.format  = PixelFormat::Depth32Float;
        shadow_desc.usage   = TextureUsage::RenderTarget | TextureUsage::ShaderRead;
        shadow_desc.storage = StorageMode::Private;

        auto shadow_map = fg.create_texture(shadow_desc, "shadow_map");
        auto gb0        = fg.create_texture(gb0_desc,    "gb0.albedo_ao");
        auto gb1        = fg.create_texture(gb1_desc,    "gb1.normal_rough_metal");
        auto depth      = fg.create_texture(depth_desc,  "depth");
        auto hdr        = fg.create_texture(hdr_desc,    "hdr");

        // Pass 1: Shadow map (depth only, scene from sun's POV).
        fg.add_pass("shadow",
            [&](PassBuilder& pb) {
                pb.write_depth(shadow_map, LoadAction::Clear, 1.0f);
            },
            [&](RenderContext& ctx) {
                auto rp = ctx.make_render_pass_desc();
                RenderEncoder enc = ctx.cmd().begin_render_pass(rp);
                enc.set_pipeline(*r->shadow_pso);

                // Spheres
                enc.set_vertex_buffer(*r->sphere_vbuf, 0);
                for (std::uint32_t i = 0; i < k_spheres.size(); ++i) {
                    enc.set_vertex_buffer(*r->draw_buf, 1, i * k_draw_stride);
                    enc.draw_indexed(r->sphere_index_count, IndexType::UInt32,
                                      *r->sphere_ibuf);
                }
                // Ground
                enc.set_vertex_buffer(*r->ground_vbuf, 0);
                enc.set_vertex_buffer(*r->draw_buf, 1, k_spheres.size() * k_draw_stride);
                enc.draw_indexed(r->ground_index_count, IndexType::UInt32,
                                  *r->ground_ibuf);
            });

        // Pass 2: GBuffer.
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

                enc.set_vertex_buffer(*r->sphere_vbuf, 0);
                for (std::uint32_t i = 0; i < k_spheres.size(); ++i) {
                    const std::size_t off = i * k_draw_stride;
                    enc.set_vertex_buffer(*r->draw_buf, 1, off);
                    enc.set_fragment_buffer(*r->draw_buf, 0, off);
                    enc.draw_indexed(r->sphere_index_count, IndexType::UInt32,
                                      *r->sphere_ibuf);
                }
                enc.set_vertex_buffer(*r->ground_vbuf, 0);
                const std::size_t off = k_spheres.size() * k_draw_stride;
                enc.set_vertex_buffer(*r->draw_buf, 1, off);
                enc.set_fragment_buffer(*r->draw_buf, 0, off);
                enc.draw_indexed(r->ground_index_count, IndexType::UInt32,
                                  *r->ground_ibuf);
            });

        // Pass 3: Lighting (samples gbuffer + depth + shadow).
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

        // Pass 4: Tonemap.
        fg.add_pass("tonemap",
            [&](PassBuilder& pb) {
                pb.read(hdr, mge::frame_graph::ResourceUsage::ShaderRead);
                pb.write_color(bb, LoadAction::Clear, 0, 0, 0, 1);
            },
            [&](RenderContext& ctx) {
                auto rp = ctx.make_render_pass_desc();
                RenderEncoder enc = ctx.cmd().begin_render_pass(rp);
                enc.set_pipeline(*r->tonemap_pso);
                enc.set_fragment_texture(ctx.texture(hdr), 0);
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
            std::printf("[hello_metal] frame %4d  last=%.2fms  avg=%.2fms (over %zu)\n",
                        frame,
                        mge::core::milliseconds(dt),
                        stats.avg_seconds() * 1000.0,
                        stats.count());
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
    if (a.headless) return run_headless();
    return run_windowed(a);
}
