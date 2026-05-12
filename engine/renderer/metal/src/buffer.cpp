#include "mge/rhi/buffer.h"

#include "mge/renderer/metal/metal_cpp.h"

namespace mge::rhi {

Buffer::~Buffer() {
    if (native_ != nullptr) {
        static_cast<MTL::Buffer*>(native_)->release();
    }
}

void* Buffer::contents() noexcept {
    if (native_ == nullptr || storage_ == StorageMode::Private ||
        storage_ == StorageMode::Memoryless) {
        return nullptr;
    }
    return static_cast<MTL::Buffer*>(native_)->contents();
}

}  // namespace mge::rhi
