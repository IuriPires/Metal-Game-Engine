// FrameGraph compile-phase tests. These exercise topological sort, lifetime
// analysis, and transient aliasing through a real Device (the graph allocates
// physical textures during compile()). They live in the integration suite
// because compile() touches the GPU when it allocates physicals; the pure
// scheduler logic is still tested here without invoking execute().

#include "mge/frame_graph/frame_graph.h"
#include "mge/rhi/rhi.h"

#include <doctest/doctest.h>

using namespace mge::rhi;
using namespace mge::frame_graph;

namespace {

TransientTextureDesc rgba8_64x64() {
    TransientTextureDesc d;
    d.width   = 64;
    d.height  = 64;
    d.format  = PixelFormat::RGBA8Unorm;
    d.usage   = TextureUsage::RenderTarget | TextureUsage::ShaderRead;
    d.storage = StorageMode::Private;
    return d;
}

TransientTextureDesc depth_64x64() {
    TransientTextureDesc d;
    d.width   = 64;
    d.height  = 64;
    d.format  = PixelFormat::Depth32Float;
    d.usage   = TextureUsage::RenderTarget;
    d.storage = StorageMode::Private;
    return d;
}

}  // namespace

TEST_CASE("FrameGraph topological sort orders producers before consumers") {
    auto device = Device::create();
    REQUIRE(device);
    FrameGraph fg(*device);

    auto a = fg.create_texture(rgba8_64x64(), "A");
    auto b = fg.create_texture(rgba8_64x64(), "B");

    // Two passes: producer writes A; consumer reads A and writes B.
    fg.add_pass("producer",
        [&](PassBuilder& pb) { pb.write_color(a); },
        [](RenderContext&) {});
    fg.add_pass("consumer",
        [&](PassBuilder& pb) {
            pb.read(a, ResourceUsage::ShaderRead);
            pb.write_color(b);
        },
        [](RenderContext&) {});

    REQUIRE(fg.compile());
    REQUIRE(fg.schedule().size() == 2);
    CHECK(fg.schedule()[0] == 0);  // producer first
    CHECK(fg.schedule()[1] == 1);
}

TEST_CASE("FrameGraph aliases two transients with disjoint lifetimes") {
    auto device = Device::create();
    REQUIRE(device);
    FrameGraph fg(*device);

    // A is written in pass 0, never read again.
    // B is written in pass 1, never read again.
    // Same desc => aliasable, lifetimes disjoint.
    auto a = fg.create_texture(rgba8_64x64(), "A");
    auto b = fg.create_texture(rgba8_64x64(), "B");

    fg.add_pass("p0", [&](PassBuilder& pb) { pb.write_color(a); }, [](RenderContext&) {});
    fg.add_pass("p1", [&](PassBuilder& pb) { pb.write_color(b); }, [](RenderContext&) {});

    REQUIRE(fg.compile());
    CHECK(fg.physical_slot(a) == fg.physical_slot(b));
    CHECK(fg.num_physical_textures() == 1);
}

TEST_CASE("FrameGraph does NOT alias overlapping lifetimes") {
    auto device = Device::create();
    REQUIRE(device);
    FrameGraph fg(*device);

    // A is written in p0 and read in p2 (lifetime spans p0..p2).
    // B is written in p1, read in p2.
    // Lifetimes overlap on p1..p2; must use distinct physicals.
    auto a = fg.create_texture(rgba8_64x64(), "A");
    auto b = fg.create_texture(rgba8_64x64(), "B");
    auto c = fg.create_texture(rgba8_64x64(), "C");

    fg.add_pass("p0", [&](PassBuilder& pb) { pb.write_color(a); }, [](RenderContext&) {});
    fg.add_pass("p1", [&](PassBuilder& pb) { pb.write_color(b); }, [](RenderContext&) {});
    fg.add_pass("p2",
        [&](PassBuilder& pb) {
            pb.read(a, ResourceUsage::ShaderRead);
            pb.read(b, ResourceUsage::ShaderRead);
            pb.write_color(c);
        },
        [](RenderContext&) {});

    REQUIRE(fg.compile());
    CHECK(fg.physical_slot(a) != fg.physical_slot(b));
    CHECK(fg.num_physical_textures() == 3);  // A, B, and C all overlap somewhere
}

TEST_CASE("FrameGraph keeps different-desc transients on distinct physicals") {
    auto device = Device::create();
    REQUIRE(device);
    FrameGraph fg(*device);

    auto color = fg.create_texture(rgba8_64x64(), "color");
    auto depth = fg.create_texture(depth_64x64(), "depth");

    fg.add_pass("pass",
        [&](PassBuilder& pb) {
            pb.write_color(color);
            pb.write_depth(depth);
        },
        [](RenderContext&) {});

    REQUIRE(fg.compile());
    CHECK(fg.physical_slot(color) != fg.physical_slot(depth));
    CHECK(fg.num_physical_textures() == 2);
}

TEST_CASE("FrameGraph dot dump mentions every pass and resource") {
    auto device = Device::create();
    REQUIRE(device);
    FrameGraph fg(*device);

    auto a = fg.create_texture(rgba8_64x64(), "alpha");
    auto b = fg.create_texture(rgba8_64x64(), "beta");
    fg.add_pass("first",  [&](PassBuilder& pb) { pb.write_color(a); }, [](RenderContext&) {});
    fg.add_pass("second", [&](PassBuilder& pb) {
        pb.read(a, ResourceUsage::ShaderRead);
        pb.write_color(b);
    }, [](RenderContext&) {});

    REQUIRE(fg.compile());
    const auto dot = fg.to_dot();
    CHECK(dot.find("digraph FrameGraph")  != std::string::npos);
    CHECK(dot.find("first")               != std::string::npos);
    CHECK(dot.find("second")              != std::string::npos);
    CHECK(dot.find("alpha")               != std::string::npos);
    CHECK(dot.find("beta")                != std::string::npos);
}
