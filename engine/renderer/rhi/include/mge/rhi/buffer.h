#pragma once

#include "mge/rhi/enums.h"

#include <cstddef>
#include <string>

namespace mge::rhi {

struct BufferDesc {
    std::size_t size              = 0;
    BufferUsage usage             = BufferUsage::Vertex;
    StorageMode storage           = StorageMode::Shared;
    const void* initial_data      = nullptr;
    std::size_t initial_data_size = 0;
    std::string label;
};

// Opaque GPU buffer. Lifetime owned by std::unique_ptr returned from
// Device::create_buffer. The backend native handle is exposed via native()
// for use by RHI-internal code (encoders, etc.) - regular renderer code
// should never call native().
class Buffer {
public:
    ~Buffer();
    Buffer(const Buffer&)            = delete;
    Buffer& operator=(const Buffer&) = delete;

    [[nodiscard]] std::size_t  size() const noexcept { return size_; }
    [[nodiscard]] StorageMode  storage() const noexcept { return storage_; }
    [[nodiscard]] BufferUsage  usage() const noexcept { return usage_; }
    [[nodiscard]] const std::string& label() const noexcept { return label_; }

    // contents() returns a CPU pointer for Shared/Managed buffers; nullptr for Private.
    [[nodiscard]] void* contents() noexcept;

    // Backend handle. Public to allow same-namespace bridge headers in the
    // backend; do not call from outside engine/renderer/.
    [[nodiscard]] void* native() noexcept { return native_; }
    [[nodiscard]] const void* native() const noexcept { return native_; }

private:
    friend class Device;
    Buffer(void* native, std::size_t size, BufferUsage usage, StorageMode storage,
           std::string label) noexcept
        : native_(native), size_(size), usage_(usage), storage_(storage),
          label_(std::move(label)) {}

    void*       native_  = nullptr;
    std::size_t size_    = 0;
    BufferUsage usage_   = BufferUsage::None;
    StorageMode storage_ = StorageMode::Private;
    std::string label_;
};

}  // namespace mge::rhi
