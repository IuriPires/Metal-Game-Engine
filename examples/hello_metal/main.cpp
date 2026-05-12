// M3 demo: open a Cocoa window, set up an RHI Device + Queue + Swapchain,
// build a render pipeline from inline MSL, and draw an interpolated-color
// triangle every frame. Frame timing is printed to stdout.
//
// Args:
//   --frames N    run for N frames then exit (default: unlimited)
//   --headless    skip window creation entirely (smoke test on CI)
//   --width W
//   --height H

#include "mge/core/time.h"
#include "mge/core/version.h"
#include "mge/platform/app.h"
#include "mge/platform/window.h"
#include "mge/rhi/rhi.h"

#include <cstdio>
#include <cstdlib>
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

// Three NDC vertices, position + RGBA per vertex.
struct Vertex {
    float pos[2];
    float color[4];
};

constexpr Vertex k_triangle[3] = {
    {{ 0.00f,  0.70f}, {1.0f, 0.20f, 0.20f, 1.0f}},   // top  - red
    {{-0.70f, -0.55f}, {0.20f, 1.0f, 0.30f, 1.0f}},   // bl   - green
    {{ 0.70f, -0.55f}, {0.30f, 0.40f, 1.0f, 1.0f}},   // br   - blue
};

constexpr const char* k_triangle_msl = R"(
    #include <metal_stdlib>
    using namespace metal;

    struct VSIn {
        float2 pos   [[attribute(0)]];
        float4 color [[attribute(1)]];
    };

    struct VSOut {
        float4 position [[position]];
        float4 color;
    };

    vertex VSOut vertex_main(VSIn in [[stage_in]]) {
        VSOut out;
        out.position = float4(in.pos, 0.0, 1.0);
        out.color    = in.color;
        return out;
    }

    fragment float4 fragment_main(VSOut in [[stage_in]]) {
        return in.color;
    }
)";

struct TriangleRenderer {
    std::unique_ptr<mge::rhi::Device>         device;
    std::unique_ptr<mge::rhi::Queue>          queue;
    std::unique_ptr<mge::rhi::Buffer>         vbuf;
    std::unique_ptr<mge::rhi::Shader>         shader;
    std::unique_ptr<mge::rhi::RenderPipeline> pso;

    static std::unique_ptr<TriangleRenderer> create(mge::rhi::PixelFormat color_format) {
        using namespace mge::rhi;
        auto r = std::make_unique<TriangleRenderer>();
        r->device = Device::create();
        if (!r->device) return nullptr;

        r->queue  = r->device->create_queue("triangle.queue");
        r->shader = r->device->create_shader_from_msl(ShaderSourceDesc{
            k_triangle_msl,
            std::string{"triangle"}});
        if (!r->queue || !r->shader) return nullptr;

        BufferDesc bd;
        bd.size              = sizeof(k_triangle);
        bd.usage             = BufferUsage::Vertex;
        bd.storage           = StorageMode::Shared;
        bd.initial_data      = k_triangle;
        bd.initial_data_size = sizeof(k_triangle);
        bd.label             = "triangle.vbuf";
        r->vbuf              = r->device->create_buffer(bd);
        if (!r->vbuf) return nullptr;

        RenderPipelineDesc pd;
        pd.vertex_shader   = r->shader.get();
        pd.vertex_entry    = "vertex_main";
        pd.fragment_shader = r->shader.get();
        pd.fragment_entry  = "fragment_main";
        pd.vertex_layout.buffers    = {VertexBufferLayout{sizeof(Vertex), false}};
        pd.vertex_layout.attributes = {
            VertexAttribute{0, VertexFormat::Float32x2, offsetof(Vertex, pos),   0},
            VertexAttribute{1, VertexFormat::Float32x4, offsetof(Vertex, color), 0},
        };
        pd.topology                  = PrimitiveTopology::TriangleList;
        pd.color_targets[0].format   = color_format;
        pd.num_color_targets         = 1;
        pd.label                     = "triangle.pso";

        r->pso = r->device->create_render_pipeline(pd);
        if (!r->pso) return nullptr;
        return r;
    }
};

int run_headless() {
    using namespace mge::rhi;
    auto tri = TriangleRenderer::create(PixelFormat::RGBA8Unorm);
    if (!tri) {
        std::fprintf(stderr, "no Metal device or pipeline failed to build\n");
        return 1;
    }
    const auto info = tri->device->info();
    std::printf("[hello_metal] headless smoke ok: device=%s\n", info.name.c_str());
    return 0;
}

int run_windowed(const Args& a) {
    using namespace mge::platform;
    using namespace mge::rhi;

    auto& app = App::get();
    app.set_name("hello_metal");

    WindowDesc wd;
    wd.title  = "MetalGameEngine - hello_metal (triangle)";
    wd.width  = a.width;
    wd.height = a.height;
    Window window(wd);

    auto tri = TriangleRenderer::create(PixelFormat::BGRA8UnormSrgb);
    if (!tri) {
        std::fprintf(stderr, "renderer init failed\n");
        return 1;
    }
    const auto info = tri->device->info();
    std::printf("[hello_metal] device: %s (unified=%d ray_tracing=%d)\n",
                info.name.c_str(), info.has_unified_memory ? 1 : 0,
                info.supports_ray_tracing ? 1 : 0);

    auto swap = tri->device->create_swapchain(window.native_layer(),
                                              PixelFormat::BGRA8UnormSrgb);
    if (!swap) {
        std::fprintf(stderr, "swapchain init failed\n");
        return 1;
    }

    mge::core::FrameStats stats;
    auto                  prev  = mge::core::now();
    int                   frame = 0;

    while (!window.should_close()) {
        app.poll_events();

        auto frame_drawable = swap->acquire_frame();
        if (!frame_drawable.valid()) {
            continue;
        }

        CommandBuffer cmd = tri->queue->create_command_buffer();

        RenderPassDesc rp;
        rp.num_color_attachments               = 1;
        rp.color_attachments[0].texture        = frame_drawable.texture();
        rp.color_attachments[0].load_action    = LoadAction::Clear;
        rp.color_attachments[0].store_action   = StoreAction::Store;
        rp.color_attachments[0].clear_color[0] = 0.04f;
        rp.color_attachments[0].clear_color[1] = 0.06f;
        rp.color_attachments[0].clear_color[2] = 0.09f;
        rp.color_attachments[0].clear_color[3] = 1.0f;
        rp.label                               = "hello_metal.pass";

        {
            RenderEncoder enc = cmd.begin_render_pass(rp);
            enc.set_pipeline(*tri->pso);
            enc.set_vertex_buffer(*tri->vbuf, 0);
            enc.draw(3);
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
