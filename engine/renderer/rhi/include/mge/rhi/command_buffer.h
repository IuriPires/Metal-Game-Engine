#pragma once

#include "mge/rhi/encoder.h"

#include <string>

namespace mge::rhi {

class Swapchain;
class SwapchainFrame;

class CommandBuffer {
public:
    ~CommandBuffer();
    CommandBuffer(const CommandBuffer&)            = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;
    CommandBuffer(CommandBuffer&&) noexcept;
    CommandBuffer& operator=(CommandBuffer&&) = delete;

    [[nodiscard]] RenderEncoder begin_render_pass(const RenderPassDesc& desc);

    void present(SwapchainFrame& frame);

    // Submit to queue.
    void commit();

    // Blocking - waits for GPU completion. Use only for tests / shutdown.
    void wait_until_completed();

    [[nodiscard]] void* native() noexcept { return native_; }

private:
    friend class Queue;
    explicit CommandBuffer(void* native) noexcept : native_(native) {}
    void* native_ = nullptr;
    bool  committed_ = false;
};

}  // namespace mge::rhi
