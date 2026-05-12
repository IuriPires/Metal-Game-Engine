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
#include "mge/core/game_loop.h"
#include "mge/core/time.h"
#include "mge/core/version.h"
#include "mge/frame_graph/frame_graph.h"
#include "mge/math/aabb.h"
#include "mge/math/frustum.h"
#include "mge/math/mat.h"
#include "mge/math/vec.h"
#include "mge/platform/app.h"
#include "mge/platform/window.h"
#include "mge/profile/profiler.h"
#include "mge/renderer/metal/metal_cpp.h"  // escape hatch for one-shot atlas upload
#include "mge/rhi/rhi.h"
#include "mge/scene/camera.h"

#include "font8x8.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

// M15 — 3-step LOD chain for the sphere mesh (high / mid / low detail).
// Index [0] is the highest LOD; the demo selects per-instance based on
// camera distance and dispatches one draw per LOD level.
constexpr std::size_t kSphereLodCount = 3;

// ---------- M16 — Skinned mesh + skeleton ----------
// 16-byte-aligned vertex with 4 joint influences per vertex. The vertex
// shader does the weighted joint blend; no compute pre-skin in v1.
struct alignas(16) SkinnedVertex {
    float         position[3];  // 12
    float         _pad0;        // 4
    float         normal[3];    // 12
    float         _pad1;        // 4
    float         weights[4];   // 16
    std::uint32_t joints[4];    // 16
};
static_assert(sizeof(SkinnedVertex) == 64);

constexpr std::uint32_t kSkinnedTubeBones = 6;

struct SkinnedMeshCpu {
    std::vector<SkinnedVertex>  vertices;
    std::vector<std::uint32_t>  indices;
};

// Procedural tube along +Y from (0,0,0) to (0,height,0), N radial segments.
// Bones are spaced uniformly along Y; each vertex blends the two nearest
// bones based on its Y position.
[[nodiscard]] inline SkinnedMeshCpu make_skinned_tube(std::uint32_t radial_segments,
                                                      std::uint32_t length_segments,
                                                      float          height,
                                                      float          radius) {
    SkinnedMeshCpu m;
    m.vertices.reserve((length_segments + 1u) * radial_segments);
    m.indices.reserve(length_segments * radial_segments * 6u);

    const float two_pi    = 6.28318530718f;
    const float bone_span = height / static_cast<float>(kSkinnedTubeBones - 1u);

    for (std::uint32_t iy = 0; iy <= length_segments; ++iy) {
        const float t = static_cast<float>(iy) / static_cast<float>(length_segments);
        const float y = t * height;
        // Continuous bone coordinate (0 → kBones-1); split into floor + frac.
        const float bf  = (y / bone_span);
        const auto  b0u = static_cast<std::uint32_t>(std::floor(bf));
        const std::uint32_t b0 = b0u >= (kSkinnedTubeBones - 1u)
                                ? kSkinnedTubeBones - 2u : b0u;
        const std::uint32_t b1 = b0 + 1u;
        const float w1 = std::clamp(bf - static_cast<float>(b0), 0.0f, 1.0f);
        const float w0 = 1.0f - w1;

        for (std::uint32_t ix = 0; ix < radial_segments; ++ix) {
            const float u  = static_cast<float>(ix) / static_cast<float>(radial_segments);
            const float a  = u * two_pi;
            const float cx = std::cos(a) * radius;
            const float cz = std::sin(a) * radius;
            SkinnedVertex v{};
            v.position[0] = cx; v.position[1] = y; v.position[2] = cz;
            v.normal[0]   = std::cos(a);
            v.normal[1]   = 0.0f;
            v.normal[2]   = std::sin(a);
            v.weights[0]  = w0; v.weights[1] = w1;
            v.weights[2]  = 0.0f; v.weights[3] = 0.0f;
            v.joints[0]   = b0; v.joints[1] = b1;
            v.joints[2]   = 0u; v.joints[3]  = 0u;
            m.vertices.push_back(v);
        }
    }

    for (std::uint32_t iy = 0; iy < length_segments; ++iy) {
        for (std::uint32_t ix = 0; ix < radial_segments; ++ix) {
            const std::uint32_t i0 = iy       * radial_segments + ix;
            const std::uint32_t i1 = iy       * radial_segments + ((ix + 1u) % radial_segments);
            const std::uint32_t i2 = (iy + 1u) * radial_segments + ix;
            const std::uint32_t i3 = (iy + 1u) * radial_segments + ((ix + 1u) % radial_segments);
            m.indices.push_back(i0); m.indices.push_back(i2); m.indices.push_back(i1);
            m.indices.push_back(i1); m.indices.push_back(i2); m.indices.push_back(i3);
        }
    }
    return m;
}

// One MSL-aligned joint matrix per bone. Vertex shader indexes by joint id.
struct alignas(16) JointBuffer {
    mge::math::Mat4 joints[kSkinnedTubeBones];
};

// Solve the bone chain: each bone has a local rotation + translation relative
// to its parent. Bone 0 is the root (no parent). For our tube the chain runs
// along +Y, so each non-root bone is placed `bone_span` above its parent.
inline void solve_bone_chain(float time, float bone_span, JointBuffer& out) {
    using namespace mge::math;
    Mat4 parent_world = Mat4::identity();
    for (std::uint32_t i = 0; i < kSkinnedTubeBones; ++i) {
        const float phase   = static_cast<float>(i) * 0.55f;
        const float wiggle  = std::sin(time * 2.5f + phase) * 0.32f;
        const Mat4  local_R = rotation_x(wiggle);
        Mat4 local;
        if (i == 0u) {
            local = local_R;                        // root rotation only
        } else {
            local = translation(Vec3{0.0f, bone_span, 0.0f}) * local_R;
        }
        const Mat4 world = parent_world * local;
        // The bind pose places bone i at y = i * bone_span with identity
        // rotation. inverse_bind = translation(0, -i*span, 0).
        const Mat4 inv_bind = translation(Vec3{0.0f,
                                                -static_cast<float>(i) * bone_span,
                                                0.0f});
        out.joints[i] = world * inv_bind;
        parent_world = world;
    }
}

struct Args {
    int           frames     = 0;
    bool          headless   = false;
    std::uint32_t width      = 1280;
    std::uint32_t height     = 720;
    std::uint32_t cube_side  = 32;
    float         time_scale = 1.0f;
    float         target_fps = 120.0f;
    float         sim_hz     = 60.0f;
    bool          paused     = false;
    bool          demo_mode  = false;  // cycle pause/slowmo/fast-fwd for visual demo
    bool          no_overlay = false;  // disable profiling overlay
    bool          no_rt      = false;  // disable RT shadows + reflections (fall back to CSM)
    int           force_lod  = -1;     // -1 = auto (distance), 0/1/2 = forced level
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
        } else if (s == "--time-scale" && i + 1 < argc) {
            a.time_scale = static_cast<float>(std::atof(argv[++i]));
        } else if (s == "--target-fps" && i + 1 < argc) {
            a.target_fps = static_cast<float>(std::atof(argv[++i]));
        } else if (s == "--sim-hz" && i + 1 < argc) {
            a.sim_hz = static_cast<float>(std::atof(argv[++i]));
        } else if (s == "--paused") {
            a.paused = true;
        } else if (s == "--demo-mode") {
            a.demo_mode = true;
        } else if (s == "--no-overlay") {
            a.no_overlay = true;
        } else if (s == "--no-rt") {
            a.no_rt = true;
        } else if (s == "--force-lod" && i + 1 < argc) {
            a.force_lod = std::atoi(argv[++i]);
            if (a.force_lod < 0 ||
                static_cast<std::size_t>(a.force_lod) >= kSphereLodCount) {
                a.force_lod = -1;
            }
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

struct alignas(16) GlyphInstance {
    float px_pos[2];
    float glyph;
    float scale;
    float color[4];
};
static_assert(sizeof(GlyphInstance) == 32);

struct alignas(16) OverlayConstants {
    float screen_px[2];
    float _pad[2];
};
static_assert(sizeof(OverlayConstants) == 16);

constexpr std::size_t k_overlay_max_glyphs = 1024;

// ---------- Particle system ----------
//
// 32-byte SoA-ish struct, one per particle. Compute kernel updates it in place.
// The render pass instances one billboard quad per live particle.
struct alignas(16) Particle {
    float position[3]; // world-space
    float life;        // 0..1, 0 = dead → respawned at the emitter
    float velocity[3]; // m/s
    float seed;        // per-particle randomization (constant)
};
static_assert(sizeof(Particle) == 32);

struct alignas(16) ParticleSimUniforms {
    float dt;
    float time;
    float life_decay;        // life lost per second
    float _pad0;
    float emitter_pos[4];    // xyz + emit_radius
    float gravity[4];        // xyz + base_speed
    float color_a[4];        // hot core (rgb) + size (a)
    float color_b[4];        // cool tail (rgb) + spawn_height
};
static_assert(sizeof(ParticleSimUniforms) == 80);

struct alignas(16) ParticleRenderUniforms {
    mge::math::Mat4 view_proj;
    float           cam_right[4];
    float           cam_up[4];
    float           color_a[4];     // matches sim color_a (with size in .a)
    float           color_b[4];     // matches sim color_b
};
static_assert(sizeof(ParticleRenderUniforms) == 128);

constexpr std::uint32_t k_particle_count = 32768;

// Build a `[N_GLYPHS * 8] x 8` R8Unorm atlas with each glyph's 8x8 bitmap.
// Returns the upload-ready bytes.
std::array<std::uint8_t, mge::overlay::k_num_glyphs * 8 * 8> build_font_atlas() {
    std::array<std::uint8_t, mge::overlay::k_num_glyphs * 8 * 8> out{};
    const std::size_t W = mge::overlay::k_num_glyphs * 8;
    for (std::size_t g = 0; g < mge::overlay::k_num_glyphs; ++g) {
        const auto& glyph = mge::overlay::k_font[g];
        for (int row = 0; row < 8; ++row) {
            const std::uint8_t byte = glyph[static_cast<std::size_t>(row)];
            for (int col = 0; col < 8; ++col) {
                const bool on = (byte >> (7 - col)) & 1u;
                const std::size_t pixel_idx =
                    static_cast<std::size_t>(row) * W + g * 8u + static_cast<std::size_t>(col);
                out[pixel_idx] = on ? 0xFF : 0x00;
            }
        }
    }
    return out;
}

// Append glyph instances for a string at (x, y) with given color/scale.
// Skips spaces (no need to emit invisible quads). Advances x = 8*scale per glyph.
inline void emit_text(std::vector<GlyphInstance>& out, float x, float y,
                       float scale, const float color[4], const char* text) {
    for (const char* p = text; *p; ++p) {
        if (*p != ' ') {
            GlyphInstance g{};
            g.px_pos[0] = x;
            g.px_pos[1] = y;
            g.glyph     = static_cast<float>(mge::overlay::glyph_index(*p));
            g.scale     = scale;
            g.color[0]  = color[0];
            g.color[1]  = color[1];
            g.color[2]  = color[2];
            g.color[3]  = color[3];
            out.push_back(g);
        }
        x += 8.0f * scale;
    }
}

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

// ---------- M17 — Compute skinning kernel ----------
//
// One thread per source vertex. Reads a SkinnedVertex, blends 4 weighted joint
// transforms, writes a PbrVertex (position + normal) to a Private output
// buffer. The G-Buffer pass then draws the tube with the standard rigid
// gbuffer pipeline — no skinning shader needed downstream. Same output is
// reusable by shadow / RT (once dynamic TLAS lands).
constexpr const char* k_skin_compute_msl = R"(
    #include <metal_stdlib>
    using namespace metal;

    struct SkinnedVertex {
        packed_float3 position; float _p0;
        packed_float3 normal;   float _p1;
        float4        weights;
        uint4         joints;
    };
    struct PbrVertex {
        packed_float3 position; float _p0;
        packed_float3 normal;   float _p1;
    };
    constant constexpr int kBones = 6;
    struct JointBuffer { float4x4 joints[kBones]; };

    kernel void skin_tube(device const SkinnedVertex* src  [[buffer(0)]],
                            device const JointBuffer&    jb   [[buffer(1)]],
                            device PbrVertex*            dst  [[buffer(2)]],
                            constant uint&               count [[buffer(3)]],
                            uint gid [[thread_position_in_grid]]) {
        if (gid >= count) return;
        SkinnedVertex v = src[gid];
        float4 lp = float4(v.position, 1.0);
        float4 ln = float4(v.normal,   0.0);
        float4 sp = float4(0);
        float4 sn = float4(0);
        for (int i = 0; i < 4; ++i) {
            float w = v.weights[i];
            if (w > 0.0) {
                float4x4 J = jb.joints[v.joints[i]];
                sp += (J * lp) * w;
                sn += (J * ln) * w;
            }
        }
        PbrVertex out;
        out.position = packed_float3(sp.xyz);
        out.normal   = packed_float3(normalize(sn.xyz));
        dst[gid] = out;
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

// ---------- M13 — Ray-traced lighting ----------
//
// Same deferred lighting math as `k_lighting_msl`, but the shadow term comes
// from an inline ray query against the TLAS, and metallic surfaces also fire
// a reflection ray that returns a hit albedo (approximated) or the sky
// gradient when nothing is hit.
constexpr const char* k_lighting_rt_msl = R"(
    #include <metal_stdlib>
    #include <metal_raytracing>
    using namespace metal;
    using namespace raytracing;
    constant float PI = 3.14159265358979323846;

    struct LightingConstants {
        float4x4 view_proj_inv;
        float4x4 light_view_proj;
        float4   camera_ws;
        float4   sun_dir_ws;
        float4   sun_color;
        float4   ambient;
        float4   shadow_params;   // .x = unused, .y = bias, .z = reflection_strength
    };

    struct VSOut { float4 position [[position]]; float2 uv; };

    vertex VSOut lighting_rt_vs(uint vid [[vertex_id]]) {
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

    // Sky color for ray misses — soft horizon gradient aligned with the sun.
    float3 sky(float3 dir, float3 sun_dir) {
        float  t       = saturate(dir.y * 0.5 + 0.5);
        float3 horizon = float3(0.50, 0.55, 0.62);
        float3 zenith  = float3(0.06, 0.12, 0.28);
        float3 col     = mix(horizon, zenith, t);
        // Faint sun halo
        float d = saturate(dot(dir, -normalize(sun_dir)));
        col += pow(d, 32.0) * float3(1.2, 0.9, 0.6) * 0.4;
        return col;
    }

    // Inline visibility ray. Returns 1.0 in light, 0.0 in shadow.
    float trace_visibility(instance_acceleration_structure tlas,
                            float3 origin, float3 dir, float tmax) {
        ray r;
        r.origin       = origin;
        r.direction    = dir;
        r.min_distance = 0.001;
        r.max_distance = tmax;
        intersector<instancing> isect;
        isect.assume_geometry_type(geometry_type::triangle);
        isect.accept_any_intersection(true);     // shadow ray: any hit ends it
        auto result = isect.intersect(r, tlas);
        return result.type == intersection_type::triangle ? 0.0 : 1.0;
    }

    fragment float4 lighting_rt_fs(VSOut in [[stage_in]],
                                     texture2d<float> gb0 [[texture(0)]],
                                     texture2d<float> gb1 [[texture(1)]],
                                     depth2d<float>   dt  [[texture(2)]],
                                     sampler          s   [[sampler(0)]],
                                     device const LightingConstants& u [[buffer(0)]],
                                     instance_acceleration_structure tlas [[buffer(1)]]) {
        float depth = dt.sample(s, in.uv);
        if (depth >= 1.0) {
            // Sky: reconstruct view ray and sample the sky gradient directly.
            float4 ndc = float4(in.uv.x * 2.0 - 1.0,
                                1.0 - in.uv.y * 2.0,
                                1.0, 1.0);
            float4 ws  = u.view_proj_inv * ndc;
            float3 dir = normalize(ws.xyz / ws.w - u.camera_ws.xyz);
            return float4(sky(dir, u.sun_dir_ws.xyz), 1.0);
        }

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

        // RT shadow: ray from the surface toward the sun, any-hit-stops.
        float bias   = max(u.shadow_params.y, 0.0005);
        float3 sP    = P + N * bias;
        float shadow = (NoL > 0.0)
            ? trace_visibility(tlas, sP, L, 200.0)
            : 0.0;

        float3 direct  = (diffuse + specular) * u.sun_color.rgb * NoL * shadow;
        float3 ambient = u.ambient.rgb * albedo * ao;
        float3 color   = direct + ambient;

        // RT reflection for metallic surfaces. Fire one ray; on miss use sky,
        // on hit approximate the hit shading with sun visibility + sky ambient.
        if (metallic > 0.5) {
            float3 R = reflect(-V, N);
            ray rr;
            rr.origin       = P + N * 0.001;
            rr.direction    = R;
            rr.min_distance = 0.001;
            rr.max_distance = 100.0;
            intersector<instancing> isect;
            isect.assume_geometry_type(geometry_type::triangle);
            auto rh = isect.intersect(rr, tlas);

            float3 refl_color;
            if (rh.type == intersection_type::triangle) {
                // Hit point. We don't have per-triangle materials in v1, so
                // approximate: pick a neutral albedo, light it with sun
                // visibility + ambient sky.
                float3 hit_pos = rr.origin + rr.direction * rh.distance;
                float3 hit_n   = normalize(-R);   // crude — we don't fetch the
                                                   // hit normal in v1.
                float vis = trace_visibility(tlas, hit_pos, L, 200.0);
                float3 hit_albedo = float3(0.7);
                float3 hit_diff   = hit_albedo / PI;
                float3 hit_amb    = u.ambient.rgb * hit_albedo;
                float hit_NoL     = saturate(dot(hit_n, L));
                refl_color = hit_diff * u.sun_color.rgb * hit_NoL * vis + hit_amb;
            } else {
                refl_color = sky(R, u.sun_dir_ws.xyz);
            }
            float refl_strength = clamp(u.shadow_params.z, 0.0, 1.0);
            color = mix(color, refl_color, metallic * refl_strength);
        }

        return float4(color, 1.0);
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

constexpr const char* k_overlay_msl = R"(
    #include <metal_stdlib>
    using namespace metal;

    struct GlyphInstance {
        float2 px_pos;
        float  glyph;
        float  scale;
        float4 color;
    };
    struct OverlayConstants {
        float2 screen_px;
        float2 _pad;
    };
    struct VSOut {
        float4 position [[position]];
        float2 uv;
        float4 color;
    };

    vertex VSOut overlay_vs(uint vid [[vertex_id]],
                             uint iid [[instance_id]],
                             device const GlyphInstance* instances [[buffer(0)]],
                             device const OverlayConstants& fc [[buffer(1)]]) {
        const float2 corners[6] = {
            float2(0, 0), float2(1, 0), float2(0, 1),
            float2(0, 1), float2(1, 0), float2(1, 1),
        };
        const float2 corner = corners[vid];
        const GlyphInstance inst = instances[iid];

        const float2 px = inst.px_pos + corner * (8.0 * inst.scale);
        // Screen-down convention -> Metal NDC y-up.
        float2 ndc = px / fc.screen_px * 2.0 - 1.0;
        ndc.y = -ndc.y;

        VSOut o;
        o.position = float4(ndc, 0.0, 1.0);
        // Atlas is 64 glyphs of 8px wide laid out horizontally.
        o.uv    = float2((inst.glyph + corner.x) / 64.0, corner.y);
        o.color = inst.color;
        return o;
    }

    fragment float4 overlay_fs(VSOut in [[stage_in]],
                                texture2d<float> atlas [[texture(0)]],
                                sampler          s     [[sampler(0)]]) {
        float a = atlas.sample(s, in.uv).r;
        return float4(in.color.rgb, in.color.a * a);
    }
)";

// ---------- Particle compute + render shaders ----------
//
// Compute kernel: gravity + integration + life decay + respawn at emitter
// (small disk on the xz plane, upward-biased random velocity).
// Render: instanced camera-facing billboard quads with a soft additive look.
constexpr const char* k_particle_compute_msl = R"(
    #include <metal_stdlib>
    using namespace metal;

    // packed_float3 has 12-byte align (4-byte) so the struct matches the C++
    // layout of `float position[3]; float life; ...` (32 B / slot). Plain
    // `float3` in device address space would pad to 16 B and misalign reads.
    struct Particle {
        packed_float3 position;
        float         life;
        packed_float3 velocity;
        float         seed;
    };

    struct SimU {
        float  dt;
        float  time;
        float  life_decay;
        float  _pad0;
        float4 emitter;   // xyz + emit_radius
        float4 gravity;   // xyz + base_speed
        float4 color_a;
        float4 color_b;
    };

    // Cheap hash → uniform [0,1)
    float hash(float x) {
        x = fract(x * 0.1031);
        x *= x + 33.33;
        x *= x + x;
        return fract(x);
    }

    kernel void particle_step(device   Particle*    p [[buffer(0)]],
                                constant SimU&        u [[buffer(1)]],
                                uint                  gid [[thread_position_in_grid]]) {
        Particle pp = p[gid];
        pp.life -= u.dt * u.life_decay;
        if (pp.life <= 0.0) {
            // Respawn: random point on a small disk around the emitter,
            // with a mostly-upward velocity perturbed by the seed.
            float t   = u.time + pp.seed * 17.0;
            float a   = hash(pp.seed * 31.0) * 6.28318;
            float r   = sqrt(hash(t)) * u.emitter.w;
            float vy  = u.gravity.w * (0.85 + 0.35 * hash(t + 1.0));
            float vx  = (hash(t + 2.0) - 0.5) * u.gravity.w * 0.45;
            float vz  = (hash(t + 3.0) - 0.5) * u.gravity.w * 0.45;
            pp.position = u.emitter.xyz + float3(cos(a) * r, 0.02, sin(a) * r);
            pp.velocity = float3(vx, vy, vz);
            pp.life     = 1.0;
        } else {
            pp.velocity += u.gravity.xyz * u.dt;
            pp.position += pp.velocity * u.dt;
        }
        p[gid] = pp;
    }
)";

constexpr const char* k_particle_render_msl = R"(
    #include <metal_stdlib>
    using namespace metal;

    // packed_float3 to match C++ layout (32 B / particle) — see compute shader.
    struct Particle {
        packed_float3 position;
        float         life;
        packed_float3 velocity;
        float         seed;
    };

    struct RenderU {
        float4x4 view_proj;
        float4   cam_right;
        float4   cam_up;
        float4   color_a;   // hot core (rgb) + size (a)
        float4   color_b;   // cool tail (rgb)
    };

    struct VSOut {
        float4 position [[position]];
        float2 uv;
        float  life;
    };

    vertex VSOut particle_vs(uint vid [[vertex_id]],
                              uint iid [[instance_id]],
                              device const Particle*  particles [[buffer(0)]],
                              constant     RenderU&   u         [[buffer(1)]]) {
        const float2 corners[6] = {
            float2(0, 0), float2(1, 0), float2(0, 1),
            float2(0, 1), float2(1, 0), float2(1, 1),
        };
        const float2 corner = corners[vid];
        Particle p          = particles[iid];

        // Collapse dead particles to a degenerate point off-screen.
        if (p.life <= 0.0) {
            VSOut o;
            o.position = float4(2.0, 2.0, 2.0, 1.0);
            o.uv       = float2(0.0);
            o.life     = 0.0;
            return o;
        }

        float2 c    = corner * 2.0 - 1.0;       // -1..1
        float  size = u.color_a.a * (0.4 + 0.6 * p.life);
        float3 ws   = p.position
                     + u.cam_right.xyz * c.x * size
                     + u.cam_up.xyz    * c.y * size;

        VSOut o;
        o.position = u.view_proj * float4(ws, 1.0);
        o.uv       = corner;
        o.life     = p.life;
        return o;
    }

    fragment float4 particle_fs(VSOut in [[stage_in]],
                                  constant RenderU& u [[buffer(0)]]) {
        float2 d = in.uv * 2.0 - 1.0;
        float  r = length(d);
        // Soft round falloff
        float a = saturate(1.0 - r);
        a       = a * a;
        // Hot core → cool tail as life fades
        float3 col = mix(u.color_b.rgb, u.color_a.rgb, in.life);
        // Pre-multiplied additive emission
        return float4(col * in.life * 2.5 * a, a);
    }
)";

// ---------- M14 — HZB build + occlusion stats ----------
//
// hzb_build: each thread covers one HZB texel. Reads the corresponding tile
// of the source gbuffer depth and writes the MAX (= farthest) depth into the
// HZB. MAX is conservative — an object is occluded iff its nearest point is
// farther than the HZB sample.
constexpr const char* k_hzb_build_msl = R"(
    #include <metal_stdlib>
    using namespace metal;

    struct HzbBuildU {
        uint src_w;
        uint src_h;
        uint dst_w;
        uint dst_h;
    };

    kernel void hzb_build(depth2d<float, access::read>    src [[texture(0)]],
                            texture2d<float, access::write> dst [[texture(1)]],
                            constant HzbBuildU&             u   [[buffer(0)]],
                            uint2 gid [[thread_position_in_grid]]) {
        if (gid.x >= u.dst_w || gid.y >= u.dst_h) return;
        uint sx0 = (gid.x       * u.src_w) / u.dst_w;
        uint sx1 = ((gid.x + 1) * u.src_w) / u.dst_w;
        uint sy0 = (gid.y       * u.src_h) / u.dst_h;
        uint sy1 = ((gid.y + 1) * u.src_h) / u.dst_h;
        if (sx1 == sx0) sx1 = sx0 + 1;
        if (sy1 == sy0) sy1 = sy0 + 1;
        float maxd = 0.0;
        for (uint y = sy0; y < sy1; ++y) {
            for (uint x = sx0; x < sx1; ++x) {
                float d = src.read(uint2(x, y));
                if (d > maxd) maxd = d;
            }
        }
        dst.write(float4(maxd), gid);
    }
)";

// hzb_stats: one thread per cube instance. Project the local unit-cube AABB
// (±0.5) through the instance model+VP, take the screen-space rect, sample
// the HZB inside it, and compare to the AABB's minimum NDC z. Writes the
// per-instance visibility flag (1 byte) and bumps an atomic counter for
// occluded cubes.
//
// v1 — only the cube field is tested; spheres/ground stay always-visible.
constexpr const char* k_hzb_stats_msl = R"(
    #include <metal_stdlib>
    using namespace metal;

    struct CullInstance {
        float4x4 model;
        float4x4 model_inv_t;
        float4   albedo_ao;
        float4   mr;
    };

    struct HzbCullU {
        float4x4 view_proj;
        uint     instance_count;     // visible cubes only
        uint     hzb_w;
        uint     hzb_h;
        uint     base_instance;      // index of the first cube in instance_buf
    };

    kernel void hzb_stats(device const CullInstance*       instances [[buffer(0)]],
                            constant     HzbCullU&            u         [[buffer(1)]],
                            texture2d<float, access::read>   hzb       [[texture(0)]],
                            device atomic_uint*              occluded  [[buffer(2)]],
                            device uchar*                    visibility [[buffer(3)]],
                            uint                              gid       [[thread_position_in_grid]]) {
        if (gid >= u.instance_count) return;
        CullInstance inst = instances[u.base_instance + gid];

        // Local AABB is the unit cube [-0.5, +0.5]^3 (the engine cube mesh).
        // Transform 8 corners through model and view_proj.
        float min_z = 1e30;
        float min_x = 1e30, max_x = -1e30;
        float min_y = 1e30, max_y = -1e30;
        bool any_behind_near = false;
        for (int i = 0; i < 8; ++i) {
            float3 local = float3((i & 1) ? 0.5 : -0.5,
                                   (i & 2) ? 0.5 : -0.5,
                                   (i & 4) ? 0.5 : -0.5);
            float4 ws   = inst.model * float4(local, 1.0);
            float4 clip = u.view_proj * ws;
            if (clip.w <= 0.001) { any_behind_near = true; continue; }
            float3 ndc = clip.xyz / clip.w;
            min_x = min(min_x, ndc.x); max_x = max(max_x, ndc.x);
            min_y = min(min_y, ndc.y); max_y = max(max_y, ndc.y);
            min_z = min(min_z, ndc.z);
        }
        // Conservative: anything straddling the near plane is visible.
        if (any_behind_near) {
            visibility[gid] = 1;
            return;
        }
        // Entirely off-screen → already frustum-culled by CPU; treat as
        // visible (don't double-bill).
        if (max_x < -1.0 || min_x > 1.0 || max_y < -1.0 || min_y > 1.0) {
            visibility[gid] = 1;
            return;
        }
        // Map NDC [-1,+1] → UV [0,1] with Y flip.
        float2 uv_min = float2(min_x, -max_y) * 0.5 + 0.5;
        float2 uv_max = float2(max_x, -min_y) * 0.5 + 0.5;
        uv_min = clamp(uv_min, 0.0, 1.0);
        uv_max = clamp(uv_max, 0.0, 1.0);
        // HZB texel rect (inclusive).
        int2 tmin = int2(floor(uv_min * float2(u.hzb_w, u.hzb_h)));
        int2 tmax = int2(ceil (uv_max * float2(u.hzb_w, u.hzb_h)));
        tmin = clamp(tmin, int2(0),
                      int2(int(u.hzb_w) - 1, int(u.hzb_h) - 1));
        tmax = clamp(tmax, int2(0),
                      int2(int(u.hzb_w) - 1, int(u.hzb_h) - 1));

        float hzb_max = 0.0;
        for (int y = tmin.y; y <= tmax.y; ++y) {
            for (int x = tmin.x; x <= tmax.x; ++x) {
                float d = hzb.read(uint2(x, y)).r;
                if (d > hzb_max) hzb_max = d;
            }
        }
        // min_z is the AABB's NEAREST depth. If even that is FARTHER than
        // HZB's MAX depth → fully behind every fragment in the rect.
        bool visible = min_z <= hzb_max;
        visibility[gid] = visible ? 1u : 0u;
        if (!visible) {
            atomic_fetch_add_explicit(occluded, 1u, memory_order_relaxed);
        }
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

    std::unique_ptr<mge::rhi::Buffer> sphere_vbuf[kSphereLodCount]{};
    std::unique_ptr<mge::rhi::Buffer> sphere_ibuf[kSphereLodCount]{};
    std::uint32_t                     sphere_index_count[kSphereLodCount]{};
    std::uint32_t                     sphere_tri_count[kSphereLodCount]{};
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
    std::unique_ptr<mge::rhi::Shader>          overlay_shader;
    std::unique_ptr<mge::rhi::RenderPipeline>  overlay_pso;
    std::unique_ptr<mge::rhi::Texture>         font_atlas;
    std::unique_ptr<mge::rhi::Buffer>          overlay_instance_buf;
    std::unique_ptr<mge::rhi::Buffer>          overlay_constants_buf;
    std::unique_ptr<mge::rhi::Sampler>         overlay_sampler;

    std::unique_ptr<mge::rhi::Shader>          particle_compute_shader;
    std::unique_ptr<mge::rhi::ComputePipeline> particle_step_pso;
    std::unique_ptr<mge::rhi::Shader>          particle_render_shader;
    std::unique_ptr<mge::rhi::RenderPipeline>  particle_render_pso;
    std::unique_ptr<mge::rhi::Buffer>          particle_buf;          // N particles
    std::unique_ptr<mge::rhi::Buffer>          particle_sim_buf;      // SimU per frame
    std::unique_ptr<mge::rhi::Buffer>          particle_render_buf;   // RenderU per frame

    // M13 — Ray tracing
    std::unique_ptr<mge::rhi::Shader>                lighting_rt_shader;
    std::unique_ptr<mge::rhi::RenderPipeline>        lighting_rt_pso;
    std::unique_ptr<mge::rhi::AccelerationStructure> blas_sphere;
    std::unique_ptr<mge::rhi::AccelerationStructure> blas_cube;
    std::unique_ptr<mge::rhi::AccelerationStructure> blas_ground;
    std::unique_ptr<mge::rhi::AccelerationStructure> tlas;

    // M16+M17 — Skinned mesh with compute-driven skinning. The source verts
    // (SkinnedVertex) live in tube_vbuf; each frame a compute kernel reads
    // them + the joint buffer and writes a deformed PbrVertex stream into
    // tube_skinned_vbuf. The G-Buffer pass then draws the tube with the
    // standard rigid gbuffer pipeline.
    std::unique_ptr<mge::rhi::Shader>           skin_compute_shader;
    std::unique_ptr<mge::rhi::ComputePipeline>  skin_compute_pso;
    std::unique_ptr<mge::rhi::Buffer>           tube_vbuf;             // SkinnedVertex
    std::unique_ptr<mge::rhi::Buffer>           tube_skinned_vbuf;     // PbrVertex out
    std::unique_ptr<mge::rhi::Buffer>           tube_skin_count_buf;   // uint count
    std::unique_ptr<mge::rhi::Buffer>           tube_ibuf;
    std::uint32_t                               tube_vertex_count = 0;
    std::uint32_t                               tube_index_count  = 0;
    std::unique_ptr<mge::rhi::Buffer>           tube_joint_buf;
    float                                       tube_bone_span    = 0.0f;
    float                                       tube_height       = 0.0f;

    // M14 — HZB occlusion culling
    std::unique_ptr<mge::rhi::Shader>           hzb_build_shader;
    std::unique_ptr<mge::rhi::ComputePipeline>  hzb_build_pso;
    std::unique_ptr<mge::rhi::Shader>           hzb_stats_shader;
    std::unique_ptr<mge::rhi::ComputePipeline>  hzb_stats_pso;
    std::unique_ptr<mge::rhi::Buffer>           hzb_build_buf;     // HzbBuildU
    std::unique_ptr<mge::rhi::Buffer>           hzb_cull_buf;      // HzbCullU
    std::unique_ptr<mge::rhi::Buffer>           hzb_counter_buf;   // atomic_uint, Shared (CPU readback)
    std::unique_ptr<mge::rhi::Buffer>           hzb_visibility_buf; // 1 byte / cube

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
        r->overlay_shader    = r->device->create_shader_from_msl({k_overlay_msl,    "overlay"});
        r->particle_compute_shader =
            r->device->create_shader_from_msl({k_particle_compute_msl, "particle.compute"});
        r->particle_render_shader  =
            r->device->create_shader_from_msl({k_particle_render_msl,  "particle.render"});
        r->lighting_rt_shader      =
            r->device->create_shader_from_msl({k_lighting_rt_msl,      "lighting.rt"});
        r->hzb_build_shader        =
            r->device->create_shader_from_msl({k_hzb_build_msl,        "hzb.build"});
        r->hzb_stats_shader        =
            r->device->create_shader_from_msl({k_hzb_stats_msl,        "hzb.stats"});
        r->skin_compute_shader     =
            r->device->create_shader_from_msl({k_skin_compute_msl,     "skin.compute"});
        if (!r->queue || !r->shadow_shader || !r->gbuffer_shader ||
            !r->lighting_shader || !r->tonemap_shader ||
            !r->bright_shader || !r->downsample_shader || !r->upsample_shader ||
            !r->overlay_shader || !r->particle_compute_shader ||
            !r->particle_render_shader || !r->lighting_rt_shader ||
            !r->hzb_build_shader || !r->hzb_stats_shader ||
            !r->skin_compute_shader) {
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

        // Sphere LOD chain (high → mid → low). Each level halves the per-axis
        // tessellation, producing roughly a 4× drop in triangle count.
        constexpr std::pair<std::uint32_t, std::uint32_t>
            sphere_lod_params[kSphereLodCount] = {
            {20u, 32u},  // high — ~640 verts
            {10u, 16u},  // mid  — ~160 verts
            { 6u, 10u},  // low  —   ~60 verts
        };
        for (std::size_t lod = 0; lod < kSphereLodCount; ++lod) {
            const auto m = mge::assets::make_sphere_pbr(sphere_lod_params[lod].first,
                                                          sphere_lod_params[lod].second);
            r->sphere_index_count[lod] = static_cast<std::uint32_t>(m.indices.size());
            r->sphere_tri_count[lod]   = r->sphere_index_count[lod] / 3u;
            const char* vlabel = lod == 0 ? "sphere.vbuf.high"
                                : lod == 1 ? "sphere.vbuf.mid" : "sphere.vbuf.low";
            const char* ilabel = lod == 0 ? "sphere.ibuf.high"
                                : lod == 1 ? "sphere.ibuf.mid" : "sphere.ibuf.low";
            r->sphere_vbuf[lod] = upload(m.vertices.data(),
                                          m.vertices.size() * sizeof(mge::assets::PbrVertex),
                                          BufferUsage::Vertex, vlabel);
            r->sphere_ibuf[lod] = upload(m.indices.data(),
                                          m.indices.size() * sizeof(std::uint32_t),
                                          BufferUsage::Index,  ilabel);
        }

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

        // Skinned tube: 16 radial × 24 length segments, 2m tall, 0.18m radius.
        // M17: source verts live in `tube_vbuf` (SkinnedVertex), the compute
        // kernel writes deformed PbrVertex into `tube_skinned_vbuf` each frame.
        {
            constexpr std::uint32_t kRadial = 16;
            constexpr std::uint32_t kLength = 24;
            constexpr float          kHeight = 2.0f;
            constexpr float          kRadius = 0.18f;
            r->tube_height    = kHeight;
            r->tube_bone_span = kHeight / static_cast<float>(kSkinnedTubeBones - 1u);
            const auto tube = make_skinned_tube(kRadial, kLength, kHeight, kRadius);
            r->tube_vertex_count = static_cast<std::uint32_t>(tube.vertices.size());
            r->tube_index_count  = static_cast<std::uint32_t>(tube.indices.size());
            r->tube_vbuf = upload(tube.vertices.data(),
                                   tube.vertices.size() * sizeof(SkinnedVertex),
                                   BufferUsage::Storage, "tube.skin_src");
            r->tube_ibuf = upload(tube.indices.data(),
                                   tube.indices.size() * sizeof(std::uint32_t),
                                   BufferUsage::Index,   "tube.ibuf");
            {
                BufferDesc d;
                d.size    = tube.vertices.size() * sizeof(mge::assets::PbrVertex);
                d.usage   = BufferUsage::Vertex | BufferUsage::Storage;
                d.storage = StorageMode::Private;   // GPU-only; gbuffer reads it
                d.label   = "tube.skin_dst";
                r->tube_skinned_vbuf = r->device->create_buffer(d);
            }
            {
                BufferDesc d;
                d.size    = sizeof(std::uint32_t);
                d.usage   = BufferUsage::Uniform;
                d.storage = StorageMode::Shared;
                d.label   = "tube.skin_count";
                r->tube_skin_count_buf = r->device->create_buffer(d);
                *static_cast<std::uint32_t*>(r->tube_skin_count_buf->contents()) =
                    r->tube_vertex_count;
            }
            BufferDesc jb;
            jb.size    = sizeof(JointBuffer);
            jb.usage   = BufferUsage::Uniform;
            jb.storage = StorageMode::Shared;
            jb.label   = "tube.joints";
            r->tube_joint_buf = r->device->create_buffer(jb);
        }

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

        // Overlay font atlas + sampler + per-frame buffers.
        {
            TextureDesc td;
            td.width   = mge::overlay::k_num_glyphs * 8u;
            td.height  = 8;
            td.format  = PixelFormat::R8Unorm;
            td.usage   = TextureUsage::ShaderRead | TextureUsage::CopyDst;
            td.storage = StorageMode::Shared;
            td.label   = "overlay.atlas";
            r->font_atlas = r->device->create_texture(td);

            // Atlas texture is Shared so we can write to its contents directly
            // via a staging buffer + blit. Simpler path: create a Shared
            // texture and copy in via `replaceRegion`. Our RHI doesn't expose
            // that yet, so use a staging buffer + blit instead.
            //
            // For Phase 1 v1 simplicity, we use a Shared-format texture and
            // pipe through a temporary buffer via blit. The atlas only loads
            // once at startup so the cost is fine.
        }

        SamplerDesc osd;
        osd.min_filter = FilterMode::Nearest;
        osd.mag_filter = FilterMode::Nearest;
        osd.address_u  = AddressMode::ClampToEdge;
        osd.address_v  = AddressMode::ClampToEdge;
        osd.label      = "overlay.sampler";
        r->overlay_sampler = r->device->create_sampler(osd);

        BufferDesc oib;
        oib.size    = sizeof(GlyphInstance) * k_overlay_max_glyphs;
        oib.usage   = BufferUsage::Uniform | BufferUsage::Storage;
        oib.storage = StorageMode::Shared;
        oib.label   = "overlay.instances";
        r->overlay_instance_buf = r->device->create_buffer(oib);

        BufferDesc ocb;
        ocb.size    = sizeof(OverlayConstants);
        ocb.usage   = BufferUsage::Uniform;
        ocb.storage = StorageMode::Shared;
        ocb.label   = "overlay.constants";
        r->overlay_constants_buf = r->device->create_buffer(ocb);

        // Particle buffers. Initialize with all-dead particles + a random seed
        // per slot so the kernel respawns them spread out over time on frame 0.
        {
            std::vector<Particle> ps(k_particle_count);
            for (std::uint32_t i = 0; i < k_particle_count; ++i) {
                // Hash-ish: distinct float per slot in [0, 1).
                const float s = static_cast<float>(i) * 0.61803398875f;
                ps[i].seed   = s - std::floor(s);
                ps[i].life   = -ps[i].seed * 0.75f;  // staggered first-spawn
                ps[i].position[0] = 0.0f;
                ps[i].position[1] = 0.0f;
                ps[i].position[2] = 0.0f;
                ps[i].velocity[0] = 0.0f;
                ps[i].velocity[1] = 0.0f;
                ps[i].velocity[2] = 0.0f;
            }
            BufferDesc pb;
            pb.size              = ps.size() * sizeof(Particle);
            pb.usage             = BufferUsage::Storage;
            pb.storage           = StorageMode::Shared;
            pb.initial_data      = ps.data();
            pb.initial_data_size = pb.size;
            pb.label             = "particles";
            r->particle_buf = r->device->create_buffer(pb);
        }
        {
            BufferDesc pu;
            pu.size    = sizeof(ParticleSimUniforms);
            pu.usage   = BufferUsage::Uniform;
            pu.storage = StorageMode::Shared;
            pu.label   = "particle.sim_u";
            r->particle_sim_buf = r->device->create_buffer(pu);
        }
        {
            BufferDesc pu;
            pu.size    = sizeof(ParticleRenderUniforms);
            pu.usage   = BufferUsage::Uniform;
            pu.storage = StorageMode::Shared;
            pu.label   = "particle.render_u";
            r->particle_render_buf = r->device->create_buffer(pu);
        }

        // M14 HZB buffers: uniforms + atomic counter (shared, CPU readback) +
        // per-instance visibility byte array (sized for the full cube field).
        {
            BufferDesc bd;
            bd.size    = 16;  // HzbBuildU = 4 uint32
            bd.usage   = BufferUsage::Uniform;
            bd.storage = StorageMode::Shared;
            bd.label   = "hzb.build_u";
            r->hzb_build_buf = r->device->create_buffer(bd);
        }
        {
            BufferDesc bd;
            bd.size    = 16 * sizeof(float) + 16;  // float4x4 + 4 u32 (some pad)
            bd.usage   = BufferUsage::Uniform;
            bd.storage = StorageMode::Shared;
            bd.label   = "hzb.cull_u";
            r->hzb_cull_buf = r->device->create_buffer(bd);
        }
        {
            BufferDesc bd;
            bd.size    = sizeof(std::uint32_t);
            bd.usage   = BufferUsage::Storage;
            bd.storage = StorageMode::Shared;     // CPU reads back the counter
            bd.label   = "hzb.counter";
            r->hzb_counter_buf = r->device->create_buffer(bd);
        }
        {
            BufferDesc bd;
            // Sized to the worst case cube count. Visibility bytes; init=1.
            bd.size    = instance_cap;
            bd.usage   = BufferUsage::Storage;
            bd.storage = StorageMode::Shared;
            bd.label   = "hzb.visibility";
            r->hzb_visibility_buf = r->device->create_buffer(bd);
            std::memset(r->hzb_visibility_buf->contents(), 1, instance_cap);
        }

        if (!r->sphere_vbuf[0] || !r->sphere_ibuf[0] || !r->ground_vbuf || !r->ground_ibuf ||
            !r->cube_vbuf || !r->cube_ibuf || !r->instance_buf || !r->frame_buf ||
            !r->lighting_buf || !r->linear_clamp || !r->shadow_sampler ||
            !r->font_atlas || !r->overlay_sampler || !r->overlay_instance_buf ||
            !r->overlay_constants_buf || !r->particle_buf ||
            !r->particle_sim_buf || !r->particle_render_buf ||
            !r->hzb_build_buf || !r->hzb_cull_buf || !r->hzb_counter_buf ||
            !r->hzb_visibility_buf) return nullptr;

        // One-shot atlas upload. Phase 1 RHI doesn't expose a buffer->texture
        // blit yet, so we drop to metal-cpp here. Logged as tech debt.
        {
            const auto bytes = build_font_atlas();
            auto* mtl_tex = static_cast<MTL::Texture*>(r->font_atlas->native());
            const MTL::Region region = MTL::Region::Make2D(
                0, 0, mge::overlay::k_num_glyphs * 8u, 8u);
            mtl_tex->replaceRegion(region, 0, bytes.data(),
                                    mge::overlay::k_num_glyphs * 8u);
        }

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

        // M17 — compute skin PSO.
        {
            ComputePipelineDesc pd;
            pd.compute_shader = r->skin_compute_shader.get();
            pd.compute_entry  = "skin_tube";
            pd.label          = "skin.compute.pso";
            r->skin_compute_pso = r->device->create_compute_pipeline(pd);
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

        // RT lighting PSO. Same color target as the rasterized variant — only
        // the fragment shader differs (ray queries instead of shadow-map PCF).
        if (r->device->info().supports_ray_tracing_from_render) {
            RenderPipelineDesc pd;
            pd.vertex_shader           = r->lighting_rt_shader.get();
            pd.fragment_shader         = r->lighting_rt_shader.get();
            pd.vertex_entry            = "lighting_rt_vs";
            pd.fragment_entry          = "lighting_rt_fs";
            pd.topology                = PrimitiveTopology::TriangleList;
            pd.color_targets[0].format = PixelFormat::RGBA16Float;
            pd.num_color_targets       = 1;
            pd.rasterizer.cull_mode    = CullMode::None;
            pd.label                   = "lighting.rt.pso";
            r->lighting_rt_pso         = r->device->create_render_pipeline(pd);
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

        // Overlay pipeline: instanced glyph quads, alpha-blended over backbuffer.
        {
            RenderPipelineDesc pd;
            pd.vertex_shader   = r->overlay_shader.get();
            pd.fragment_shader = r->overlay_shader.get();
            pd.vertex_entry    = "overlay_vs";
            pd.fragment_entry  = "overlay_fs";
            pd.topology        = PrimitiveTopology::TriangleList;
            pd.color_targets[0].format    = backbuffer_fmt;
            pd.color_targets[0].blend     = true;
            pd.color_targets[0].src_color = BlendFactor::SrcAlpha;
            pd.color_targets[0].dst_color = BlendFactor::OneMinusSrcAlpha;
            pd.color_targets[0].color_op  = BlendOp::Add;
            pd.color_targets[0].src_alpha = BlendFactor::One;
            pd.color_targets[0].dst_alpha = BlendFactor::OneMinusSrcAlpha;
            pd.color_targets[0].alpha_op  = BlendOp::Add;
            pd.num_color_targets          = 1;
            pd.rasterizer.cull_mode       = CullMode::None;
            pd.label                      = "overlay.pso";
            r->overlay_pso                = r->device->create_render_pipeline(pd);
        }

        // Particle compute pipeline (one kernel: gravity + integration + respawn).
        {
            ComputePipelineDesc pd;
            pd.compute_shader = r->particle_compute_shader.get();
            pd.compute_entry  = "particle_step";
            pd.label          = "particle.step.pso";
            r->particle_step_pso = r->device->create_compute_pipeline(pd);
        }

        // HZB build + cull-stats compute pipelines.
        {
            ComputePipelineDesc pd;
            pd.compute_shader = r->hzb_build_shader.get();
            pd.compute_entry  = "hzb_build";
            pd.label          = "hzb.build.pso";
            r->hzb_build_pso  = r->device->create_compute_pipeline(pd);
        }
        {
            ComputePipelineDesc pd;
            pd.compute_shader = r->hzb_stats_shader.get();
            pd.compute_entry  = "hzb_stats";
            pd.label          = "hzb.stats.pso";
            r->hzb_stats_pso  = r->device->create_compute_pipeline(pd);
        }

        // Particle render pipeline: instanced billboard quads, additive emission
        // over the tonemapped HDR (post-tonemap pass writes to sRGB, but additive
        // still reads fine — the visual is "fire-glow over scene").
        {
            RenderPipelineDesc pd;
            pd.vertex_shader   = r->particle_render_shader.get();
            pd.fragment_shader = r->particle_render_shader.get();
            pd.vertex_entry    = "particle_vs";
            pd.fragment_entry  = "particle_fs";
            pd.topology        = PrimitiveTopology::TriangleList;
            pd.color_targets[0].format    = backbuffer_fmt;
            pd.color_targets[0].blend     = true;
            pd.color_targets[0].src_color = BlendFactor::One;
            pd.color_targets[0].dst_color = BlendFactor::One;
            pd.color_targets[0].color_op  = BlendOp::Add;
            pd.color_targets[0].src_alpha = BlendFactor::One;
            pd.color_targets[0].dst_alpha = BlendFactor::One;
            pd.color_targets[0].alpha_op  = BlendOp::Add;
            pd.num_color_targets          = 1;
            pd.rasterizer.cull_mode       = CullMode::None;
            pd.label                      = "particle.render.pso";
            r->particle_render_pso        = r->device->create_render_pipeline(pd);
        }

        if (!r->shadow_pso || !r->gbuffer_pso || !r->lighting_pso || !r->tonemap_pso ||
            !r->bright_pso || !r->downsample_pso || !r->upsample_pso || !r->overlay_pso ||
            !r->particle_step_pso || !r->particle_render_pso ||
            !r->hzb_build_pso || !r->hzb_stats_pso ||
            !r->skin_compute_pso || !r->tube_vbuf || !r->tube_skinned_vbuf ||
            !r->tube_skin_count_buf || !r->tube_ibuf || !r->tube_joint_buf) {
            return nullptr;
        }

        // Per-mesh BLAS (sphere, cube, ground). Position is at offset 0 in
        // PbrVertex (stride 32). Indexed Float32x3 geometry.
        if (r->device->info().supports_ray_tracing) {
            auto build_blas = [&](Buffer& vbuf, std::uint32_t vcount, Buffer& ibuf,
                                   std::uint32_t icount, const char* label) {
                TriangleGeometryDesc tg;
                tg.vertex_buffer  = &vbuf;
                tg.vertex_stride  = sizeof(mge::assets::PbrVertex);
                tg.vertex_count   = vcount;
                tg.vertex_format  = VertexFormat::Float32x3;
                tg.index_buffer   = &ibuf;
                tg.index_type     = IndexType::UInt32;
                tg.triangle_count = icount / 3u;
                tg.opaque         = true;
                PrimitiveAccelDesc d;
                d.geometries.push_back(tg);
                d.label = label;
                return r->device->build_acceleration_structure(*r->queue, d);
            };
            const auto sphere_geom = mge::assets::make_sphere_pbr(20, 32);
            const auto ground_geom = mge::assets::make_ground_plane_pbr(30.0f);
            const auto cube_geom   = mge::assets::make_cube_pbr();
            // RT BVH uses the high-LOD sphere mesh — reflections always sample
            // the highest detail (rasterization picks LOD per camera distance).
            r->blas_sphere = build_blas(
                *r->sphere_vbuf[0], static_cast<std::uint32_t>(sphere_geom.vertices.size()),
                *r->sphere_ibuf[0], r->sphere_index_count[0], "blas.sphere");
            r->blas_ground = build_blas(
                *r->ground_vbuf, static_cast<std::uint32_t>(ground_geom.vertices.size()),
                *r->ground_ibuf, r->ground_index_count, "blas.ground");
            r->blas_cube   = build_blas(
                *r->cube_vbuf,   static_cast<std::uint32_t>(cube_geom.vertices.size()),
                *r->cube_ibuf,   r->cube_index_count,   "blas.cube");
            if (!r->blas_sphere || !r->blas_ground || !r->blas_cube) {
                std::fprintf(stderr, "[hello_metal] BLAS build failed\n");
                return nullptr;
            }
        }

        return r;
    }

    // Build the top-level acceleration structure once the scene layout is
    // known. v1 is static — no refit, no per-frame rebuild. Spheres, ground,
    // and every cube get one instance.
    bool build_tlas(std::span<const Sphere> spheres, std::span<const CubeProto> cubes) {
        using namespace mge::rhi;
        if (!device->info().supports_ray_tracing) return false;
        if (!blas_sphere || !blas_cube || !blas_ground) return false;

        InstanceAccelDesc d;
        d.blas = {blas_sphere.get(), blas_ground.get(), blas_cube.get()};
        d.label = "scene.tlas";

        auto identity_rotation_3x3 = std::array<float, 9>{
            1, 0, 0,
            0, 1, 0,
            0, 0, 1};
        auto make_inst = [](std::uint32_t blas_idx, mge::math::Vec3 t,
                             std::array<float, 9> rs) {
            AccelInstance i;
            i.transform_3x4 = {
                rs[0], rs[1], rs[2], t.x,
                rs[3], rs[4], rs[5], t.y,
                rs[6], rs[7], rs[8], t.z,
            };
            i.blas_index = blas_idx;
            return i;
        };

        for (const auto& s : spheres) {
            d.instances.push_back(make_inst(0u, s.position, identity_rotation_3x3));
        }
        d.instances.push_back(make_inst(1u, mge::math::Vec3{0.0f, 0.0f, 0.0f},
                                          identity_rotation_3x3));
        const std::array<float, 9> cube_scale_3x3{
            k_cube_half, 0, 0,
            0, k_cube_half, 0,
            0, 0, k_cube_half};
        for (const auto& c : cubes) {
            d.instances.push_back(make_inst(2u, c.center, cube_scale_3x3));
        }

        tlas = device->build_acceleration_structure(*queue, d);
        return tlas != nullptr;
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
    u.shadow_params[1] = 0.0015f;                                 // RT bias / CSM legacy
    u.shadow_params[2] = 0.65f;                                   // RT reflection strength
    u.shadow_params[3] = 0.0f;
    std::memcpy(r.lighting_buf->contents(), &u, sizeof(u));
}

int run_headless(std::size_t instance_cap) {
    auto r = DeferredRenderer::create(mge::rhi::PixelFormat::BGRA8UnormSrgb, instance_cap);
    if (!r) { std::fprintf(stderr, "headless: init failed\n"); return 1; }
    const auto info = r->device->info();
    std::printf("[hello_metal] headless smoke ok: device=%s, sphere LODs %u/%u/%u idx, cube %u idx\n",
                info.name.c_str(),
                r->sphere_index_count[0], r->sphere_index_count[1], r->sphere_index_count[2],
                r->cube_index_count);
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
    std::printf("[hello_metal] device: %s, instance_cap=%zu  RT=%s\n",
                info.name.c_str(), instance_cap,
                info.supports_ray_tracing_from_render ? "yes" : "no");

    // M13 — build the scene TLAS once. RT lighting flips on at runtime if the
    // device supports it and --no-rt wasn't passed.
    const bool rt_available = info.supports_ray_tracing_from_render && !a.no_rt
                              && r->lighting_rt_pso;
    bool rt_active = false;
    if (rt_available) {
        rt_active = r->build_tlas(std::span<const Sphere>(k_spheres.data(), k_spheres.size()),
                                   std::span<const CubeProto>(cubes_proto.data(),
                                                                cubes_proto.size()));
        std::printf("[hello_metal] RT: %s (TLAS over %zu instances)\n",
                    rt_active ? "on" : "off (TLAS build failed)",
                    k_spheres.size() + 1u + cubes_proto.size());
    } else {
        std::printf("[hello_metal] RT: off (%s)\n",
                    a.no_rt ? "--no-rt" : "device unsupported");
    }

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
    std::vector<GlyphInstance> glyphs;
    glyphs.reserve(k_overlay_max_glyphs);

    // GameLoop: fixed-timestep sim (60 Hz default) + decoupled render with
    // pacing. The cube wobble advances in the sim callback; the render
    // callback interpolates with `alpha` for smooth visuals between steps.
    mge::core::GameLoop loop;
    loop.set_fixed_dt(1.0f / a.sim_hz);
    loop.set_target_fps(a.target_fps);
    loop.set_time_scale(a.time_scale);
    loop.set_paused(a.paused);

    std::printf("[hello_metal] loop: sim %.0fHz, target %.0ffps, scale %.2f%s%s\n",
                static_cast<double>(a.sim_hz),
                static_cast<double>(a.target_fps),
                static_cast<double>(a.time_scale),
                a.paused ? " [paused]" : "",
                a.demo_mode ? " [demo cycle]" : "");

    float         sim_yaw            = 0.0f;  // accumulated by sim_fn
    std::uint32_t cube_visible_count = 0;     // updated by render_fn

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

        // Demo mode cycles through (normal, paused, slow-mo, fast-fwd) every
        // ~3 seconds of wall time so you can visually confirm each state.
        if (a.demo_mode) {
            const double wall   = mge::core::seconds(mge::core::now() - origin);
            const double phase  = std::fmod(wall, 12.0);
            if (phase < 3.0)        { loop.set_paused(false); loop.set_time_scale(1.0f); }
            else if (phase < 5.0)   { loop.set_paused(true);  loop.set_time_scale(1.0f); }
            else if (phase < 8.0)   { loop.set_paused(false); loop.set_time_scale(0.25f); }
            else                    { loop.set_paused(false); loop.set_time_scale(3.0f); }
        }

        const auto sim_fn = [&](float dt) {
            sim_yaw += dt * 0.4f;
        };

        bool rendered = false;
        const auto render_fn = [&](float alpha) {
            auto frame_drawable = swap->acquire_frame();
            if (!frame_drawable.valid()) return;
            rendered = true;

        // M13: spheres are static so the rendered scene matches the static
        // TLAS. The previous M11 wobble around the world origin would have
        // de-synced RT shadows from the rasterized sphere positions.
        // `alpha` is used by the M16 skinned-tube animation for smooth motion
        // between sim ticks.
        (void)sim_yaw;

        // ---- LOD selection (CPU, distance-based) ----
        // Pick per-sphere LOD by camera distance. The instance buffer is
        // then packed in LOD-grouped order [LOD0 ... LOD1 ... LOD2], so each
        // LOD's draw can use base_instance + instance_count without
        // gathering.
        std::array<std::uint8_t, k_spheres.size()> sphere_lod{};
        std::array<std::uint32_t, kSphereLodCount> sphere_lod_counts{};
        {
            const mge::math::Vec3 cam = camera.eye();
            for (std::size_t i = 0; i < k_spheres.size(); ++i) {
                if (a.force_lod >= 0) {
                    sphere_lod[i] = static_cast<std::uint8_t>(a.force_lod);
                } else {
                    const auto p = k_spheres[i].position;
                    const float dx = p.x - cam.x;
                    const float dy = p.y - cam.y;
                    const float dz = p.z - cam.z;
                    const float d  = std::sqrt(dx * dx + dy * dy + dz * dz);
                    std::uint8_t lod = 0;
                    if (d > 18.0f)      lod = 2;
                    else if (d > 9.0f)  lod = 1;
                    sphere_lod[i] = lod;
                }
                ++sphere_lod_counts[sphere_lod[i]];
            }
        }
        std::array<std::uint32_t, kSphereLodCount> sphere_lod_offsets{};
        for (std::size_t lod = 1; lod < kSphereLodCount; ++lod) {
            sphere_lod_offsets[lod] =
                sphere_lod_offsets[lod - 1] + sphere_lod_counts[lod - 1];
        }

        // ---- Profile: instance buffer fill (CPU work) ----
        InstanceData* instances = static_cast<InstanceData*>(r->instance_buf->contents());
        // Instance layout: [spheres × N, ground, tube, cubes...]
        const std::uint32_t tube_slot = static_cast<std::uint32_t>(k_spheres.size() + 1u);
        const std::uint32_t cube_base = tube_slot + 1u;
        {
            MGE_PROFILE_ZONE("fill_instances");
        // Spheres at [0..5), packed in LOD-grouped order.
        std::array<std::uint32_t, kSphereLodCount> lod_cursor{};
        for (std::size_t lod = 0; lod < kSphereLodCount; ++lod) {
            lod_cursor[lod] = sphere_lod_offsets[lod];
        }
        for (std::size_t i = 0; i < k_spheres.size(); ++i) {
            const auto&     s     = k_spheres[i];
            const mge::math::Mat4 m = mge::math::translation(s.position);
            InstanceData    inst{};
            inst.model       = m;
            inst.model_inv_t = mge::math::transpose(mge::math::inverse(m));
            inst.albedo_ao[0] = s.albedo.x; inst.albedo_ao[1] = s.albedo.y;
            inst.albedo_ao[2] = s.albedo.z; inst.albedo_ao[3] = s.ao;
            inst.mr[0]        = s.metallic; inst.mr[1] = s.roughness;
            inst.mr[2]        = 0.0f;       inst.mr[3] = 0.0f;
            const std::uint8_t lod = sphere_lod[i];
            instances[lod_cursor[lod]++] = inst;
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
        // Tube (skinned) at [tube_slot]: stand in front of the camera, slightly
        // to the right of the sphere row.
        {
            InstanceData inst{};
            const mge::math::Mat4 m =
                mge::math::translation(mge::math::Vec3{0.0f, 0.0f, 3.6f});
            inst.model        = m;
            inst.model_inv_t  = mge::math::transpose(mge::math::inverse(m));
            inst.albedo_ao[0] = 0.18f; inst.albedo_ao[1] = 0.55f;
            inst.albedo_ao[2] = 0.70f; inst.albedo_ao[3] = 1.0f;
            inst.mr[0]        = 0.0f;  inst.mr[1] = 0.35f;
            inst.mr[2]        = 0.0f;  inst.mr[3] = 0.0f;
            instances[tube_slot] = inst;
        }

        // Frustum-cull the cube field against the camera.
        {
            MGE_PROFILE_ZONE("cull");
            const mge::math::Frustum cam_frustum =
                mge::math::Frustum::from_view_projection(camera.view_projection());
            visible_cubes.clear();
            for (std::uint32_t i = 0; i < cubes_proto.size(); ++i) {
                if (mge::math::aabb_visible(cam_frustum, cube_world_aabb(cubes_proto[i]))) {
                    visible_cubes.push_back(i);
                }
            }
        }
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

        cube_visible_count = static_cast<std::uint32_t>(visible_cubes.size());
        }  // end of fill_instances profile zone

        // M16 — bone animation. Solve the chain on CPU each frame; upload one
        // JointBuffer to GPU; the skinned vertex shader blends 4 joint
        // influences per vertex. Wall-clock time so it stays smooth at any
        // render rate; pause+time_scale handled by reusing sim_time.
        {
            MGE_PROFILE_ZONE("skin_anim");
            JointBuffer jb{};
            const float t = static_cast<float>(loop.sim_time())
                            + alpha * loop.fixed_dt();
            solve_bone_chain(t, r->tube_bone_span, jb);
            std::memcpy(r->tube_joint_buf->contents(), &jb, sizeof(jb));
        }

        // ---- Particle sim + render uniforms ----
        // Wall-clock dt so the visual stays smooth at any render rate. Respects
        // pause and time_scale so demo-mode cycles affect particles too.
        {
            MGE_PROFILE_ZONE("particle_uniforms");
            static auto p_last = mge::core::now();
            const auto  p_now  = mge::core::now();
            float       p_dt   = static_cast<float>(mge::core::seconds(p_now - p_last));
            p_last = p_now;
            if (p_dt < 0.0f)  p_dt = 0.0f;
            if (p_dt > 0.05f) p_dt = 0.05f;        // clamp big stalls
            if (loop.is_paused()) p_dt = 0.0f;
            p_dt *= loop.time_scale();

            ParticleSimUniforms su{};
            su.dt             = p_dt;
            su.time           = static_cast<float>(loop.sim_time());
            su.life_decay     = 0.55f;             // ~1.8s expected lifetime
            su.emitter_pos[0] = 0.0f;
            su.emitter_pos[1] = 0.10f;
            su.emitter_pos[2] = 0.0f;
            su.emitter_pos[3] = 0.55f;             // emit disk radius
            su.gravity[0]     =  0.0f;
            su.gravity[1]     = -2.0f;             // m/s^2
            su.gravity[2]     =  0.0f;
            su.gravity[3]     =  4.5f;             // base upward speed
            // Hot core (mostly used by render), cool tail (mostly used by render).
            su.color_a[0] = 1.00f; su.color_a[1] = 0.85f; su.color_a[2] = 0.45f;
            su.color_a[3] = 0.06f;                  // billboard radius (m)
            su.color_b[0] = 1.00f; su.color_b[1] = 0.20f; su.color_b[2] = 0.05f;
            su.color_b[3] = 0.0f;
            std::memcpy(r->particle_sim_buf->contents(), &su, sizeof(su));

            ParticleRenderUniforms ru{};
            ru.view_proj = camera.view_projection();
            const auto fwd = camera.forward();
            const mge::math::Vec3 world_up{0.0f, 1.0f, 0.0f};
            const auto right = mge::math::normalize(mge::math::cross(fwd, world_up));
            const auto up    = mge::math::cross(right, fwd);
            ru.cam_right[0] = right.x; ru.cam_right[1] = right.y; ru.cam_right[2] = right.z;
            ru.cam_right[3] = 0.0f;
            ru.cam_up[0]    = up.x;    ru.cam_up[1]    = up.y;    ru.cam_up[2]    = up.z;
            ru.cam_up[3]    = 0.0f;
            ru.color_a[0] = su.color_a[0]; ru.color_a[1] = su.color_a[1];
            ru.color_a[2] = su.color_a[2]; ru.color_a[3] = su.color_a[3];
            ru.color_b[0] = su.color_b[0]; ru.color_b[1] = su.color_b[1];
            ru.color_b[2] = su.color_b[2]; ru.color_b[3] = 0.0f;
            std::memcpy(r->particle_render_buf->contents(), &ru, sizeof(ru));
        }

        // ---- Build the overlay glyph instance buffer with the latest stats ----
        std::uint32_t overlay_glyph_count = 0;
        if (!a.no_overlay) {
            MGE_PROFILE_ZONE("overlay_build");
            glyphs.clear();

            const auto zones = mge::profile::Profiler::get().snapshot();
            const float fps_now = stats.last_seconds() > 0.0
                ? static_cast<float>(1.0 / stats.last_seconds()) : 0.0f;
            const float ms_now  = static_cast<float>(stats.last_seconds() * 1000.0);
            const float ms_avg  = static_cast<float>(stats.avg_seconds() * 1000.0);

            const float fg_w = static_cast<float>(window.drawable_width());
            const float fg_h = static_cast<float>(window.drawable_height());
            OverlayConstants oc{};
            oc.screen_px[0] = fg_w;
            oc.screen_px[1] = fg_h;
            std::memcpy(r->overlay_constants_buf->contents(), &oc, sizeof(oc));

            constexpr float kScale  = 2.0f;
            constexpr float kPad    = 16.0f;
            constexpr float kLineH  = 8.0f * kScale + 4.0f;
            const float     yellow[4] = {1.0f, 0.85f, 0.20f, 1.0f};
            const float     white[4]  = {0.95f, 0.97f, 1.0f, 1.0f};
            const float     gray[4]   = {0.65f, 0.70f, 0.78f, 1.0f};
            const float     warn[4]   = {1.0f,  0.40f, 0.30f, 1.0f};

            char buf[80];
            float y = kPad;

            std::snprintf(buf, sizeof(buf), "METALGAMEENGINE  PHASE 1");
            emit_text(glyphs, kPad, y, kScale, yellow, buf);
            y += kLineH;

            std::snprintf(buf, sizeof(buf), "FPS %5.1f  MS %6.2f  AVG %6.2f",
                          static_cast<double>(fps_now),
                          static_cast<double>(ms_now),
                          static_cast<double>(ms_avg));
            emit_text(glyphs, kPad, y, kScale, white, buf);
            y += kLineH;

            std::snprintf(buf, sizeof(buf), "SIM %5.2fS  STEPS %llu  ALPHA %.2f",
                          loop.sim_time(),
                          static_cast<unsigned long long>(loop.step_count()),
                          static_cast<double>(loop.last_alpha()));
            emit_text(glyphs, kPad, y, kScale, gray, buf);
            y += kLineH;

            std::snprintf(buf, sizeof(buf), "CUBES %5u  /  %zu",
                          cube_visible_count, cubes_proto.size());
            emit_text(glyphs, kPad, y, kScale, gray, buf);
            y += kLineH;

            // HZB occlusion stats from the PREVIOUS frame (the counter buffer
            // was written by last frame's hzb.stats compute pass; readback is
            // safe here since the FG already committed + GPU has progressed).
            const std::uint32_t hzb_occluded =
                *static_cast<const std::uint32_t*>(r->hzb_counter_buf->contents());
            const std::uint32_t hzb_visible = cube_visible_count > hzb_occluded
                ? cube_visible_count - hzb_occluded : 0u;
            std::snprintf(buf, sizeof(buf), "HZB  %5u VIS  %5u OCC", hzb_visible, hzb_occluded);
            emit_text(glyphs, kPad, y, kScale, gray, buf);
            y += kLineH;

            // M15 — LOD distribution + total sphere triangles drawn this frame.
            const std::uint32_t lod_tris =
                sphere_lod_counts[0] * r->sphere_tri_count[0] +
                sphere_lod_counts[1] * r->sphere_tri_count[1] +
                sphere_lod_counts[2] * r->sphere_tri_count[2];
            const std::uint32_t baseline_tris =
                static_cast<std::uint32_t>(k_spheres.size()) * r->sphere_tri_count[0];
            std::snprintf(buf, sizeof(buf),
                          "LOD H%u M%u L%u  TRIS %u/%u",
                          sphere_lod_counts[0], sphere_lod_counts[1], sphere_lod_counts[2],
                          lod_tris, baseline_tris);
            emit_text(glyphs, kPad, y, kScale, gray, buf);
            y += kLineH;

            std::snprintf(buf, sizeof(buf), "SKIN BONES %u  VERTS %u  (COMPUTE)",
                          static_cast<unsigned>(kSkinnedTubeBones),
                          r->tube_vertex_count);
            emit_text(glyphs, kPad, y, kScale, gray, buf);
            y += kLineH;

            const char* state = loop.is_paused() ? "PAUSED"
                              : (loop.time_scale() < 0.99f ? "SLOW"
                                : (loop.time_scale() > 1.01f ? "FAST" : "REAL"));
            std::snprintf(buf, sizeof(buf), "STATE %s  SCALE %.2f",
                          state, static_cast<double>(loop.time_scale()));
            emit_text(glyphs, kPad, y, kScale, loop.is_paused() ? warn : gray, buf);
            y += kLineH + 4.0f;

            // CPU profile zones (last_ms / avg_ms over the rolling window)
            emit_text(glyphs, kPad, y, kScale, yellow, "CPU ZONES");
            y += kLineH;
            for (const auto& z : zones) {
                std::snprintf(buf, sizeof(buf), "%-14.*s LAST %5.3f  AVG %5.3f MS",
                              static_cast<int>(z.name.size()), z.name.data(),
                              z.last_ms, z.avg_ms);
                // Convert to uppercase since the font is upper-case only.
                for (char& c : buf) {
                    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
                }
                emit_text(glyphs, kPad, y, kScale, white, buf);
                y += kLineH;
            }

            if (glyphs.size() > k_overlay_max_glyphs) glyphs.resize(k_overlay_max_glyphs);
            overlay_glyph_count = static_cast<std::uint32_t>(glyphs.size());
            std::memcpy(r->overlay_instance_buf->contents(), glyphs.data(),
                        glyphs.size() * sizeof(GlyphInstance));
        }

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

        constexpr std::uint32_t k_hzb_w = 256;
        constexpr std::uint32_t k_hzb_h = 256;
        TransientTextureDesc hzb_desc{
            k_hzb_w, k_hzb_h, 1, PixelFormat::R32Float,
            TextureUsage::ShaderRead | TextureUsage::ShaderWrite, StorageMode::Private};

        auto shadow_map = fg.create_texture(shadow_desc, "shadow_map");
        auto gb0        = fg.create_texture(gb0_desc,    "gb0");
        auto gb1        = fg.create_texture(gb1_desc,    "gb1");
        auto depth      = fg.create_texture(depth_desc,  "depth");
        auto hdr        = fg.create_texture(hdr_desc,    "hdr");
        auto hzb_tex    = fg.create_texture(hzb_desc,    "hzb");

        fg.add_pass("shadow",
            [&](PassBuilder& pb) {
                pb.write_depth(shadow_map, LoadAction::Clear, 1.0f);
            },
            [&, sphere_lod_counts, sphere_lod_offsets](RenderContext& ctx) {
                auto rp = ctx.make_render_pass_desc();
                RenderEncoder enc = ctx.cmd().begin_render_pass(rp);
                enc.set_pipeline(*r->shadow_pso);
                enc.set_vertex_buffer(*r->frame_buf, 2);

                for (std::size_t lod = 0; lod < kSphereLodCount; ++lod) {
                    if (sphere_lod_counts[lod] == 0) continue;
                    enc.set_vertex_buffer(*r->sphere_vbuf[lod], 0);
                    enc.set_vertex_buffer(*r->instance_buf, 1,
                                            sphere_lod_offsets[lod] * sizeof(InstanceData));
                    enc.draw_indexed(r->sphere_index_count[lod], IndexType::UInt32,
                                      *r->sphere_ibuf[lod], 0, sphere_lod_counts[lod]);
                }

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

        // M17 — skin compute pass. Runs once per frame before gbuffer; writes
        // the deformed tube vertex buffer that gbuffer + (future) shadow + RT
        // will consume. No FG buffer handles yet (P1-FG-BUFFER-001), so the
        // ordering between this pass and gbuffer is established by
        // declaration order on the same command buffer.
        fg.add_pass("skin",
            [&](PassBuilder&) {},
            [&](RenderContext& ctx) {
                auto enc = ctx.cmd().begin_compute_pass("skin.tube");
                enc.set_pipeline(*r->skin_compute_pso);
                enc.set_buffer(*r->tube_vbuf,           0);
                enc.set_buffer(*r->tube_joint_buf,      1);
                enc.set_buffer(*r->tube_skinned_vbuf,   2);
                enc.set_buffer(*r->tube_skin_count_buf, 3);
                const std::uint32_t tg =
                    r->skin_compute_pso->thread_execution_width();
                enc.dispatch_threads(r->tube_vertex_count, 1, 1, tg, 1, 1);
            });

        fg.add_pass("gbuffer",
            [&](PassBuilder& pb) {
                pb.write_color(gb0,   LoadAction::Clear, 0, 0, 0, 0);
                pb.write_color(gb1,   LoadAction::Clear, 0, 0, 0, 0);
                pb.write_depth(depth, LoadAction::Clear, 1.0f);
            },
            [&, sphere_lod_counts, sphere_lod_offsets](RenderContext& ctx) {
                auto rp = ctx.make_render_pass_desc();
                RenderEncoder enc = ctx.cmd().begin_render_pass(rp);
                enc.set_pipeline(*r->gbuffer_pso);
                enc.set_vertex_buffer(*r->frame_buf, 2);

                for (std::size_t lod = 0; lod < kSphereLodCount; ++lod) {
                    if (sphere_lod_counts[lod] == 0) continue;
                    const std::size_t off = sphere_lod_offsets[lod] * sizeof(InstanceData);
                    enc.set_vertex_buffer(*r->sphere_vbuf[lod], 0);
                    enc.set_vertex_buffer(*r->instance_buf,    1, off);
                    enc.set_fragment_buffer(*r->instance_buf,  0, off);
                    enc.draw_indexed(r->sphere_index_count[lod], IndexType::UInt32,
                                      *r->sphere_ibuf[lod], 0, sphere_lod_counts[lod]);
                }

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

                // M17 — skinned tube draws with the rigid gbuffer PSO over the
                // compute-skinned vertex buffer. No skinning math in the
                // vertex stage; the compute pass that ran before this one
                // already deformed `tube_skinned_vbuf` in place.
                {
                    const std::size_t off = tube_slot * sizeof(InstanceData);
                    enc.set_pipeline(*r->gbuffer_pso);   // re-bind rigid pso
                    enc.set_vertex_buffer(*r->tube_skinned_vbuf, 0);
                    enc.set_vertex_buffer(*r->instance_buf,   1, off);
                    enc.set_fragment_buffer(*r->instance_buf, 0, off);
                    enc.draw_indexed(r->tube_index_count, IndexType::UInt32,
                                      *r->tube_ibuf, 0, 1);
                }
            });

        // M14 HZB build — max-reduce gbuffer depth into a 256x256 R32F.
        {
            struct HzbBuildU { std::uint32_t src_w, src_h, dst_w, dst_h; };
            HzbBuildU bu{fw, fh, k_hzb_w, k_hzb_h};
            std::memcpy(r->hzb_build_buf->contents(), &bu, sizeof(bu));
        }
        fg.add_pass("hzb.build",
            [&](PassBuilder& pb) {
                pb.read(depth,   mge::frame_graph::ResourceUsage::ShaderRead);
                pb.write(hzb_tex, mge::frame_graph::ResourceUsage::ShaderWrite);
            },
            [&](RenderContext& ctx) {
                auto enc = ctx.cmd().begin_compute_pass("hzb.build");
                enc.set_pipeline(*r->hzb_build_pso);
                enc.set_texture(ctx.texture(depth),   0);
                enc.set_texture(ctx.texture(hzb_tex), 1);
                enc.set_buffer(*r->hzb_build_buf, 0);
                const std::uint32_t tg =
                    r->hzb_build_pso->thread_execution_width() >= 32 ? 8u : 4u;
                enc.dispatch_threads(k_hzb_w, k_hzb_h, 1, tg, tg, 1);
            });

        // M14 HZB stats — per-cube AABB projection + HZB max-rect test.
        // Reads the visible cube instance subset; writes occluded counter
        // (Shared, CPU readback) + per-instance visibility bytes.
        if (cube_visible_count > 0) {
            struct HzbCullU {
                mge::math::Mat4 view_proj;
                std::uint32_t   instance_count;
                std::uint32_t   hzb_w;
                std::uint32_t   hzb_h;
                std::uint32_t   base_instance;
            };
            HzbCullU cu{};
            cu.view_proj      = camera.view_projection();
            cu.instance_count = cube_visible_count;
            cu.hzb_w          = k_hzb_w;
            cu.hzb_h          = k_hzb_h;
            cu.base_instance  = cube_base;
            std::memcpy(r->hzb_cull_buf->contents(), &cu, sizeof(cu));

            // Clear the atomic counter on CPU side (Shared buffer).
            *static_cast<std::uint32_t*>(r->hzb_counter_buf->contents()) = 0u;
        }
        fg.add_pass("hzb.stats",
            [&](PassBuilder& pb) {
                pb.read(hzb_tex, mge::frame_graph::ResourceUsage::ShaderRead);
            },
            [&, count = cube_visible_count](RenderContext& ctx) {
                if (count == 0) return;
                auto enc = ctx.cmd().begin_compute_pass("hzb.stats");
                enc.set_pipeline(*r->hzb_stats_pso);
                enc.set_buffer(*r->instance_buf,       0);
                enc.set_buffer(*r->hzb_cull_buf,       1);
                enc.set_buffer(*r->hzb_counter_buf,    2);
                enc.set_buffer(*r->hzb_visibility_buf, 3);
                enc.set_texture(ctx.texture(hzb_tex),  0);
                const std::uint32_t tg =
                    r->hzb_stats_pso->thread_execution_width();
                enc.dispatch_threads(count, 1, 1, tg, 1, 1);
            });

        fg.add_pass("lighting",
            [&](PassBuilder& pb) {
                pb.read(gb0,        mge::frame_graph::ResourceUsage::ShaderRead);
                pb.read(gb1,        mge::frame_graph::ResourceUsage::ShaderRead);
                pb.read(depth,      mge::frame_graph::ResourceUsage::ShaderRead);
                pb.read(shadow_map, mge::frame_graph::ResourceUsage::ShaderRead);
                pb.write_color(hdr, LoadAction::Clear, 0, 0, 0, 1);
            },
            [&, rt_active](RenderContext& ctx) {
                auto rp = ctx.make_render_pass_desc();
                RenderEncoder enc = ctx.cmd().begin_render_pass(rp);
                if (rt_active) {
                    enc.set_pipeline(*r->lighting_rt_pso);
                    enc.set_fragment_texture(ctx.texture(gb0),   0);
                    enc.set_fragment_texture(ctx.texture(gb1),   1);
                    enc.set_fragment_texture(ctx.texture(depth), 2);
                    enc.set_fragment_sampler(*r->linear_clamp,   0);
                    enc.set_fragment_buffer(*r->lighting_buf, 0);
                    // TLAS at buffer slot 1; mark each BLAS used so Metal
                    // keeps them resident through the pass.
                    enc.use_fragment_acceleration_structure(*r->blas_sphere);
                    enc.use_fragment_acceleration_structure(*r->blas_cube);
                    enc.use_fragment_acceleration_structure(*r->blas_ground);
                    enc.set_fragment_acceleration_structure(*r->tlas, 1);
                } else {
                    enc.set_pipeline(*r->lighting_pso);
                    enc.set_fragment_texture(ctx.texture(gb0),        0);
                    enc.set_fragment_texture(ctx.texture(gb1),        1);
                    enc.set_fragment_texture(ctx.texture(depth),      2);
                    enc.set_fragment_texture(ctx.texture(shadow_map), 3);
                    enc.set_fragment_sampler(*r->linear_clamp,        0);
                    enc.set_fragment_sampler(*r->shadow_sampler,      1);
                    enc.set_fragment_buffer(*r->lighting_buf, 0);
                }
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

        // Particles: GPU-driven step + additive billboard render.
        // The compute step shares this pass's command buffer; FG schedules it
        // after tonemap (WAW on bb) and before overlay (also WAW on bb).
        fg.add_pass("particles",
            [&](PassBuilder& pb) {
                pb.write_color(bb, LoadAction::Load, 0, 0, 0, 1);
            },
            [&](RenderContext& ctx) {
                {
                    auto cenc = ctx.cmd().begin_compute_pass("particle.step");
                    cenc.set_pipeline(*r->particle_step_pso);
                    cenc.set_buffer(*r->particle_buf,     0);
                    cenc.set_buffer(*r->particle_sim_buf, 1);
                    const std::uint32_t tg =
                        r->particle_step_pso->thread_execution_width();
                    cenc.dispatch_threads(k_particle_count, 1, 1, tg, 1, 1);
                }
                auto rp = ctx.make_render_pass_desc();
                RenderEncoder enc = ctx.cmd().begin_render_pass(rp);
                enc.set_pipeline(*r->particle_render_pso);
                enc.set_vertex_buffer(*r->particle_buf,        0);
                enc.set_vertex_buffer(*r->particle_render_buf, 1);
                enc.set_fragment_buffer(*r->particle_render_buf, 0);
                enc.draw(6, k_particle_count);
            });

        // Overlay pass: draws glyphs over the backbuffer with alpha blend.
        if (!a.no_overlay && overlay_glyph_count > 0) {
            const std::uint32_t draw_count = overlay_glyph_count;
            fg.add_pass("overlay",
                [&](PassBuilder& pb) {
                    pb.write_color(bb, LoadAction::Load, 0, 0, 0, 1);
                },
                [&, draw_count](RenderContext& ctx) {
                    auto rp = ctx.make_render_pass_desc();
                    RenderEncoder enc = ctx.cmd().begin_render_pass(rp);
                    enc.set_pipeline(*r->overlay_pso);
                    enc.set_vertex_buffer(*r->overlay_instance_buf, 0);
                    enc.set_vertex_buffer(*r->overlay_constants_buf, 1);
                    enc.set_fragment_texture(*r->font_atlas,     0);
                    enc.set_fragment_sampler(*r->overlay_sampler, 0);
                    enc.draw(6, draw_count);
                });
        }

        {
            MGE_PROFILE_ZONE("fg_compile");
            if (!fg.compile()) {
                std::fprintf(stderr, "frame graph compile failed\n");
                return;
            }
        }
        {
            MGE_PROFILE_ZONE("fg_execute");
            fg.execute(*r->queue, &frame_drawable);
        }
        };  // render_fn

        loop.tick(sim_fn, render_fn);

        if (!rendered) continue;  // no drawable this iteration

        const auto now = mge::core::now();
        const auto dt  = now - prev;
        prev           = now;
        stats.push(mge::core::seconds(dt));

        ++frame;
        if (frame % 60 == 0) {
            std::printf("[hello_metal] frame %4d  last=%.2fms  avg=%.2fms"
                        "  cubes %u/%zu  sim=%.2fs  steps=%llu  alpha=%.2f%s\n",
                        frame,
                        mge::core::milliseconds(dt),
                        stats.avg_seconds() * 1000.0,
                        cube_visible_count, cubes_proto.size(),
                        loop.sim_time(),
                        static_cast<unsigned long long>(loop.step_count()),
                        static_cast<double>(loop.last_alpha()),
                        loop.is_paused() ? " [PAUSED]"
                            : (loop.time_scale() < 0.99f ? " [SLOW]"
                                : (loop.time_scale() > 1.01f ? " [FAST]" : "")));
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
