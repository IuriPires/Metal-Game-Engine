// M1 demo: open a Cocoa window with a CAMetalLayer-backed view, encode a
// clear-only render pass each frame, present, and print frame timing.
//
// Args:
//   --frames N       run for N frames then exit (default: unlimited)
//   --headless       skip window creation entirely (smoke test on CI)
//   --width W
//   --height H

#include "mge/core/time.h"
#include "mge/core/version.h"
#include "mge/platform/app.h"
#include "mge/platform/window.h"
#include "mge/renderer/metal/clear_color.h"
#include "mge/renderer/metal/clear_renderer.h"
#include "mge/renderer/metal/device.h"
#include "mge/renderer/metal/swapchain.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

namespace {

struct Args {
    int           frames    = 0;       // 0 = unlimited
    bool          headless  = false;
    std::uint32_t width     = 1280;
    std::uint32_t height    = 720;
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

int run_headless() {
    auto* dev = mge::renderer::metal::Device::create();
    if (dev == nullptr) {
        std::fprintf(stderr, "no Metal device\n");
        return 1;
    }
    std::printf("[hello_metal] headless smoke ok: device=%s\n", dev->name().c_str());
    delete dev;
    return 0;
}

int run_windowed(const Args& a) {
    using namespace mge::platform;
    using namespace mge::renderer::metal;

    auto& app = App::get();
    app.set_name("hello_metal");

    WindowDesc wd;
    wd.title  = "MetalGameEngine - hello_metal";
    wd.width  = a.width;
    wd.height = a.height;
    Window window(wd);

    std::unique_ptr<Device> device{Device::create()};
    if (!device) {
        std::fprintf(stderr, "no Metal device\n");
        return 1;
    }
    std::printf("[hello_metal] device: %s (unified=%d ray_tracing=%d)\n",
                device->name().c_str(),
                device->has_unified_memory() ? 1 : 0,
                device->supports_ray_tracing() ? 1 : 0);

    Swapchain     swap(*device, window.metal_layer());
    ClearRenderer renderer(*device, swap);

    mge::core::FrameStats stats;
    auto                  prev = mge::core::now();
    int                   frame = 0;

    while (!window.should_close()) {
        app.poll_events();

        // Animate clear color so the user can see the frame loop is alive.
        const double t = mge::core::seconds(mge::core::now().time_since_epoch());
        renderer.set_clear_color(ClearColor{
            0.5 * (1.0 + std::sin(t * 0.7)),
            0.5 * (1.0 + std::sin(t * 1.1 + 2.0)),
            0.5 * (1.0 + std::sin(t * 1.3 + 4.0)),
            1.0,
        });

        renderer.draw();

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
    std::printf("[hello_metal] %s %s (build=%s)\n",
                mge::core::engine_name().data(),
                "0.0.1",
                mge::core::engine_build_kind().data());
    if (a.headless) {
        return run_headless();
    }
    return run_windowed(a);
}
