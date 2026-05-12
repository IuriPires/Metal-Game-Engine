#pragma once

#include "mge/renderer/metal/metal_cpp.h"

#include <string>

namespace mge::renderer::metal {

// Thin RAII wrapper around MTLDevice + a default MTLCommandQueue. This is the
// "hello Metal" abstraction for M1. The real RHI Device lands at M3.
class Device {
public:
    // Acquire the system default device. Returns nullptr on failure (no Metal
    // capable device available).
    [[nodiscard]] static Device* create();

    Device(const Device&)            = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&)                 = delete;
    Device& operator=(Device&&)      = delete;

    ~Device();

    [[nodiscard]] MTL::Device*       mtl() noexcept { return device_; }
    [[nodiscard]] MTL::CommandQueue* queue() noexcept { return queue_; }

    [[nodiscard]] std::string name() const;
    [[nodiscard]] bool        supports_ray_tracing() const noexcept;
    [[nodiscard]] bool        is_low_power() const noexcept;
    [[nodiscard]] bool        has_unified_memory() const noexcept;

private:
    Device() = default;

    MTL::Device*       device_ = nullptr;
    MTL::CommandQueue* queue_  = nullptr;
};

}  // namespace mge::renderer::metal
