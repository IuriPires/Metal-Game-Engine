#pragma once

#include "mge/rhi/command_buffer.h"

#include <string>

namespace mge::rhi {

class Queue {
public:
    ~Queue();
    Queue(const Queue&)            = delete;
    Queue& operator=(const Queue&) = delete;

    [[nodiscard]] CommandBuffer create_command_buffer();

    [[nodiscard]] void* native() noexcept { return native_; }

private:
    friend class Device;
    Queue(void* native, std::string label) noexcept
        : native_(native), label_(std::move(label)) {}

    void*       native_ = nullptr;
    std::string label_;
};

}  // namespace mge::rhi
