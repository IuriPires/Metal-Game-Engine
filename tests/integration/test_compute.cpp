// Compute test: build a compute pipeline from inline MSL, dispatch it,
// and assert the kernel wrote the expected pattern into a Shared buffer.

#include "mge/rhi/rhi.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace mge::rhi;

namespace {

constexpr const char* k_fill_kernel_msl = R"(
    #include <metal_stdlib>
    using namespace metal;

    // out[i] = i * 2 + bias
    kernel void fill(device uint*                out  [[buffer(0)]],
                      device const uint&         bias [[buffer(1)]],
                      uint                       gid  [[thread_position_in_grid]]) {
        out[gid] = gid * 2u + bias;
    }
)";

}  // namespace

TEST_CASE("Compute pipeline compiles and dispatches a fill kernel") {
    auto device = Device::create();
    REQUIRE(device);
    auto queue = device->create_queue("compute.test.queue");
    REQUIRE(queue);

    constexpr std::uint32_t N = 256;

    BufferDesc out_desc;
    out_desc.size    = N * sizeof(std::uint32_t);
    out_desc.usage   = BufferUsage::Storage | BufferUsage::CopySrc;
    out_desc.storage = StorageMode::Shared;
    out_desc.label   = "compute.out";
    auto out_buf = device->create_buffer(out_desc);
    REQUIRE(out_buf);

    const std::uint32_t bias = 7u;
    BufferDesc bias_desc;
    bias_desc.size              = sizeof(std::uint32_t);
    bias_desc.usage             = BufferUsage::Uniform;
    bias_desc.storage           = StorageMode::Shared;
    bias_desc.initial_data      = &bias;
    bias_desc.initial_data_size = sizeof(std::uint32_t);
    bias_desc.label             = "compute.bias";
    auto bias_buf = device->create_buffer(bias_desc);
    REQUIRE(bias_buf);

    auto shader = device->create_shader_from_msl({k_fill_kernel_msl, "compute.fill"});
    REQUIRE(shader);

    ComputePipelineDesc pd;
    pd.compute_shader = shader.get();
    pd.compute_entry  = "fill";
    pd.label          = "compute.fill.pso";
    auto pso = device->create_compute_pipeline(pd);
    REQUIRE(pso);

    CHECK(pso->thread_execution_width() > 0);
    CHECK(pso->max_total_threads_per_threadgroup() >= 64);

    {
        auto cmd = queue->create_command_buffer();
        {
            auto enc = cmd.begin_compute_pass("compute.test");
            enc.set_pipeline(*pso);
            enc.set_buffer(*out_buf,  0);
            enc.set_buffer(*bias_buf, 1);
            const std::uint32_t tg = pso->thread_execution_width();
            enc.dispatch_threads(N, 1, 1, tg, 1, 1);
        }
        cmd.commit();
        cmd.wait_until_completed();
    }

    const auto* result = static_cast<const std::uint32_t*>(out_buf->contents());
    REQUIRE(result != nullptr);
    for (std::uint32_t i = 0; i < N; ++i) {
        CHECK(result[i] == i * 2u + bias);
    }
}
