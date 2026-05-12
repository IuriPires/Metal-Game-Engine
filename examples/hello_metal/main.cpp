// M4 demo: open a Cocoa window, build a Lambert-lit rotating cube via the
// RHI. Per-frame uniforms (view-projection, model, model-inv-transpose,
// directional light, ambient) are uploaded to a Shared MTL::Buffer. Depth
// test enabled; depth texture is reallocated on resize. CCW back-face cull.
//
// Args:
//   --frames N    run for N frames then exit (default: unlimited)
//   --headless    skip window creation; do a smoke check of the pipeline
//   --width W
//   --height H

#include "mge/assets/mesh.h"
#include "mge/assets/primitives.h"
#include "mge/core/time.h"
#include "mge/core/version.h"
#include "mge/math/mat.h"
#include "mge/math/quat.h"
#include "mge/math/vec.h"
#include "mge/platform/app.h"
#include "mge/platform/window.h"
#include "mge/rhi/rhi.h"
#include "mge/scene/camera.h"

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

// Layout matches the MSL `Uniforms` struct in the inline shader below.
struct alignas(16) LambertUniforms {
    mge::math::Mat4 view_proj;       //  64 B
    mge::math::Mat4 model;           //  64 B
    mge::math::Mat4 model_inv_t;     //  64 B
    float           light_dir_ws[4]; //  16 B  (direction TO the surface)
    float           light_color[4];  //  16 B  (rgb intensity, a unused)
    float           ambient[4];      //  16 B
};
static_assert(sizeof(LambertUniforms) == 240);

constexpr const char* k_lambert_msl = R"(
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

    struct VSIn {
        float3 position [[attribute(0)]];
        float3 normal   [[attribute(1)]];
        float4 color    [[attribute(2)]];
    };

    struct VSOut {
        float4 position [[position]];
        float3 normal_ws;
        float4 color;
    };

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
        float3 ambient = u.ambient.xyz;
        float3 lit     = in.color.rgb * (diffuse + ambient);
        return float4(lit, 1.0);
    }
)";

struct CubeRenderer {
    std::unique_ptr<mge::rhi::Device>         device;
    std::unique_ptr<mge::rhi::Queue>          queue;
    std::unique_ptr<mge::rhi::Buffer>         vbuf;
    std::unique_ptr<mge::rhi::Buffer>         ibuf;
    std::unique_ptr<mge::rhi::Buffer>         ubuf;
    std::unique_ptr<mge::rhi::Shader>         shader;
    std::unique_ptr<mge::rhi::RenderPipeline> pso;
    std::unique_ptr<mge::rhi::Texture>        depth;
    std::uint32_t                             index_count = 0;
    mge::rhi::PixelFormat                     color_fmt   = mge::rhi::PixelFormat::Undefined;
    mge::rhi::PixelFormat                     depth_fmt   = mge::rhi::PixelFormat::Depth32Float;

    static std::unique_ptr<CubeRenderer> create(mge::rhi::PixelFormat color_format) {
        using namespace mge::rhi;
        auto r       = std::make_unique<CubeRenderer>();
        r->color_fmt = color_format;
        r->device    = Device::create();
        if (!r->device) return nullptr;

        r->queue  = r->device->create_queue("cube.queue");
        r->shader = r->device->create_shader_from_msl(
            ShaderSourceDesc{k_lambert_msl, std::string{"lambert"}});
        if (!r->queue || !r->shader) return nullptr;

        // Mesh.
        const auto mesh = mge::assets::make_cube({0.85f, 0.55f, 0.25f, 1.0f});
        r->index_count  = static_cast<std::uint32_t>(mesh.indices.size());

        BufferDesc vb;
        vb.size              = mesh.vertices.size() * sizeof(mge::assets::LambertVertex);
        vb.usage             = BufferUsage::Vertex;
        vb.storage           = StorageMode::Shared;
        vb.initial_data      = mesh.vertices.data();
        vb.initial_data_size = vb.size;
        vb.label             = "cube.vbuf";
        r->vbuf              = r->device->create_buffer(vb);

        BufferDesc ib;
        ib.size              = mesh.indices.size() * sizeof(std::uint32_t);
        ib.usage             = BufferUsage::Index;
        ib.storage           = StorageMode::Shared;
        ib.initial_data      = mesh.indices.data();
        ib.initial_data_size = ib.size;
        ib.label             = "cube.ibuf";
        r->ibuf              = r->device->create_buffer(ib);

        BufferDesc ub;
        ub.size    = sizeof(LambertUniforms);
        ub.usage   = BufferUsage::Uniform;
        ub.storage = StorageMode::Shared;
        ub.label   = "cube.ubuf";
        r->ubuf    = r->device->create_buffer(ub);

        if (!r->vbuf || !r->ibuf || !r->ubuf) return nullptr;

        // Pipeline.
        RenderPipelineDesc pd;
        pd.vertex_shader   = r->shader.get();
        pd.fragment_shader = r->shader.get();
        pd.vertex_entry    = "vertex_main";
        pd.fragment_entry  = "fragment_main";
        pd.vertex_layout.buffers    = {VertexBufferLayout{sizeof(mge::assets::LambertVertex), false}};
        pd.vertex_layout.attributes = {
            VertexAttribute{0, VertexFormat::Float32x3, offsetof(mge::assets::LambertVertex, position), 0},
            VertexAttribute{1, VertexFormat::Float32x3, offsetof(mge::assets::LambertVertex, normal),   0},
            VertexAttribute{2, VertexFormat::Float32x4, offsetof(mge::assets::LambertVertex, color),    0},
        };
        pd.topology                  = PrimitiveTopology::TriangleList;
        pd.color_targets[0].format   = color_format;
        pd.num_color_targets         = 1;
        pd.depth.format              = r->depth_fmt;
        pd.depth.write_enabled       = true;
        pd.depth.compare             = DepthCompare::Less;
        pd.rasterizer.cull_mode      = CullMode::Back;
        pd.rasterizer.front_face     = FrontFace::CounterClockwise;
        pd.label                     = "cube.pso";
        r->pso                       = r->device->create_render_pipeline(pd);
        if (!r->pso) return nullptr;

        return r;
    }

    void ensure_depth(std::uint32_t w, std::uint32_t h) {
        if (depth && depth->width() == w && depth->height() == h) return;
        using namespace mge::rhi;
        TextureDesc td;
        td.width   = w;
        td.height  = h;
        td.format  = depth_fmt;
        td.usage   = TextureUsage::RenderTarget;
        td.storage = StorageMode::Private;
        td.label   = "cube.depth";
        depth      = device->create_texture(td);
    }
};

void fill_uniforms(LambertUniforms& u, const mge::scene::Camera& cam, float t) {
    using namespace mge::math;
    const Mat4 model = rotation_y(t * 0.8f) * rotation_x(t * 0.5f);
    u.view_proj      = cam.view_projection();
    u.model          = model;
    u.model_inv_t    = transpose(inverse(model));

    // Light coming from upper-right-front, pointing toward origin.
    const Vec3 ldir = normalize(Vec3{-0.6f, -0.8f, -0.5f});
    u.light_dir_ws[0] = ldir.x;
    u.light_dir_ws[1] = ldir.y;
    u.light_dir_ws[2] = ldir.z;
    u.light_dir_ws[3] = 1.0f;

    u.light_color[0] = 1.0f;
    u.light_color[1] = 0.95f;
    u.light_color[2] = 0.85f;
    u.light_color[3] = 1.0f;

    u.ambient[0] = 0.12f;
    u.ambient[1] = 0.14f;
    u.ambient[2] = 0.18f;
    u.ambient[3] = 0.0f;
}

int run_headless() {
    auto r = CubeRenderer::create(mge::rhi::PixelFormat::RGBA8Unorm);
    if (!r) {
        std::fprintf(stderr, "headless: pipeline init failed\n");
        return 1;
    }
    const auto info = r->device->info();
    std::printf("[hello_metal] headless smoke ok: device=%s, %u indices\n",
                info.name.c_str(), r->index_count);
    return 0;
}

int run_windowed(const Args& a) {
    using namespace mge::platform;
    using namespace mge::rhi;

    auto& app = App::get();
    app.set_name("hello_metal");

    WindowDesc wd;
    wd.title  = "MetalGameEngine - hello_metal (Lambert cube)";
    wd.width  = a.width;
    wd.height = a.height;
    Window window(wd);

    auto r = CubeRenderer::create(PixelFormat::BGRA8UnormSrgb);
    if (!r) {
        std::fprintf(stderr, "renderer init failed\n");
        return 1;
    }
    const auto info = r->device->info();
    std::printf("[hello_metal] device: %s (unified=%d ray_tracing=%d)\n",
                info.name.c_str(), info.has_unified_memory ? 1 : 0,
                info.supports_ray_tracing ? 1 : 0);

    auto swap = r->device->create_swapchain(window.native_layer(),
                                            PixelFormat::BGRA8UnormSrgb);
    if (!swap) {
        std::fprintf(stderr, "swapchain init failed\n");
        return 1;
    }

    auto sync_to_drawable = [&]() {
        const std::uint32_t w = window.drawable_width();
        const std::uint32_t h = window.drawable_height();
        if (w == 0 || h == 0) return;
        swap->resize(w, h);
        r->ensure_depth(w, h);
    };
    sync_to_drawable();

    mge::scene::Camera camera;
    camera.set_perspective(mge::math::radians(60.0f),
                           static_cast<float>(r->depth->width()) /
                               static_cast<float>(r->depth->height()),
                           0.1f, 100.0f);
    camera.look_at({2.5f, 1.8f, 3.0f}, {0, 0, 0}, {0, 1, 0});

    mge::core::FrameStats stats;
    auto                  prev   = mge::core::now();
    auto                  origin = mge::core::now();
    int                   frame  = 0;

    while (!window.should_close()) {
        app.poll_events();

        if (window.consume_resize_event()) {
            sync_to_drawable();
            camera.set_aspect(static_cast<float>(r->depth->width()) /
                              static_cast<float>(r->depth->height()));
        }

        auto frame_drawable = swap->acquire_frame();
        if (!frame_drawable.valid()) {
            continue;
        }

        const float t = static_cast<float>(mge::core::seconds(mge::core::now() - origin));
        LambertUniforms u{};
        fill_uniforms(u, camera, t);
        std::memcpy(r->ubuf->contents(), &u, sizeof(u));

        CommandBuffer cmd = r->queue->create_command_buffer();

        RenderPassDesc rp;
        rp.num_color_attachments               = 1;
        rp.color_attachments[0].texture        = frame_drawable.texture();
        rp.color_attachments[0].load_action    = LoadAction::Clear;
        rp.color_attachments[0].store_action   = StoreAction::Store;
        rp.color_attachments[0].clear_color[0] = 0.04f;
        rp.color_attachments[0].clear_color[1] = 0.06f;
        rp.color_attachments[0].clear_color[2] = 0.09f;
        rp.color_attachments[0].clear_color[3] = 1.0f;
        rp.has_depth                           = true;
        rp.depth_attachment.texture            = r->depth.get();
        rp.depth_attachment.load_action        = LoadAction::Clear;
        rp.depth_attachment.store_action       = StoreAction::DontCare;
        rp.depth_attachment.clear_depth        = 1.0f;
        rp.label                               = "hello_metal.cube";

        {
            RenderEncoder enc = cmd.begin_render_pass(rp);
            enc.set_pipeline(*r->pso);
            enc.set_vertex_buffer(*r->vbuf, 0);
            enc.set_vertex_buffer(*r->ubuf, 1);
            enc.set_fragment_buffer(*r->ubuf, 0);
            enc.draw_indexed(r->index_count, IndexType::UInt32, *r->ibuf);
        }
        cmd.present(frame_drawable);
        cmd.commit();

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

        if (a.frames > 0 && frame >= a.frames) {
            window.request_close();
        }
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
        return run_headless();
    }
    return run_windowed(a);
}
