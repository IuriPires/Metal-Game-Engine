#include "mge/rhi/queue.h"

#include "mge/renderer/metal/metal_cpp.h"

namespace mge::rhi {

Queue::~Queue() {
    if (native_ != nullptr) {
        static_cast<MTL::CommandQueue*>(native_)->release();
    }
}

CommandBuffer Queue::create_command_buffer() {
    auto* q   = static_cast<MTL::CommandQueue*>(native_);
    auto* cmd = q->commandBuffer();
    // MTLCommandBuffer is autoreleased - retain so the C++ wrapper owns it.
    if (cmd != nullptr) {
        cmd->retain();
    }
    return CommandBuffer(cmd);
}

}  // namespace mge::rhi
