#include "mge/renderer/metal/device.h"

namespace mge::renderer::metal {

Device* Device::create() {
    MTL::Device* dev = MTL::CreateSystemDefaultDevice();
    if (dev == nullptr) {
        return nullptr;
    }
    MTL::CommandQueue* q = dev->newCommandQueue();
    if (q == nullptr) {
        dev->release();
        return nullptr;
    }
    auto* d     = new Device();
    d->device_ = dev;
    d->queue_  = q;
    return d;
}

Device::~Device() {
    if (queue_ != nullptr) {
        queue_->release();
    }
    if (device_ != nullptr) {
        device_->release();
    }
}

std::string Device::name() const {
    NS::String* s = device_->name();
    return std::string(s->utf8String());
}

bool Device::supports_ray_tracing() const noexcept {
    return device_->supportsRaytracing();
}

bool Device::is_low_power() const noexcept {
    return device_->lowPower();
}

bool Device::has_unified_memory() const noexcept {
    return device_->hasUnifiedMemory();
}

}  // namespace mge::renderer::metal
