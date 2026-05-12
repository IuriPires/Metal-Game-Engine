#pragma once

#include "mge/rhi/enums.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mge::rhi {

class Buffer;

// One geometry inside a primitive acceleration structure. v1 supports only
// triangle geometry (indexed or non-indexed). Curves / bounding-box / motion
// AABBs aren't modeled — when they show up we add separate descriptor types.
struct TriangleGeometryDesc {
    Buffer*       vertex_buffer = nullptr;
    std::size_t   vertex_offset = 0;
    std::uint32_t vertex_stride = 12;            // bytes per vertex (pos-only = 12)
    std::uint32_t vertex_count  = 0;
    VertexFormat  vertex_format = VertexFormat::Float32x3;

    Buffer*       index_buffer  = nullptr;       // nullptr → non-indexed
    std::size_t   index_offset  = 0;
    IndexType     index_type    = IndexType::UInt32;

    std::uint32_t triangle_count = 0;            // (indexed: index_count/3, else vertex_count/3)
    bool          opaque         = true;         // skips any-hit programs
};

struct PrimitiveAccelDesc {
    std::vector<TriangleGeometryDesc> geometries;
    std::string                       label;
};

class AccelerationStructure;

// One instance in a top-level (instance) acceleration structure. The transform
// is row-major 3x4 (i.e. 3 rows of 4 columns); Metal packs it as PackedFloat4x3.
struct AccelInstance {
    std::array<float, 12> transform_3x4{};
    std::uint32_t         blas_index = 0;        // index into InstanceAccelDesc::blas
    std::uint32_t         mask       = 0xFFu;
    bool                  opaque     = true;
};

struct InstanceAccelDesc {
    std::vector<AccelerationStructure*> blas;
    std::vector<AccelInstance>          instances;
    std::string                         label;
};

// Opaque acceleration structure. Holds either a primitive (BLAS) or an instance
// (TLAS) MTL::AccelerationStructure. Lifetime is owned by std::unique_ptr from
// Device::build_acceleration_structure().
class AccelerationStructure {
public:
    ~AccelerationStructure();
    AccelerationStructure(const AccelerationStructure&)            = delete;
    AccelerationStructure& operator=(const AccelerationStructure&) = delete;

    [[nodiscard]] void*       native() noexcept { return native_; }
    [[nodiscard]] const void* native() const noexcept { return native_; }
    [[nodiscard]] const std::string& label() const noexcept { return label_; }
    [[nodiscard]] bool        is_instance() const noexcept { return is_instance_; }

    // Internal: construct from a native handle. Public so backend TUs can mint
    // structures without being friends. Callers outside the RHI must not use.
    AccelerationStructure(void* native, bool is_instance, std::string label) noexcept
        : native_(native), is_instance_(is_instance), label_(std::move(label)) {}

private:
    void*       native_      = nullptr;
    bool        is_instance_ = false;
    std::string label_;
};

}  // namespace mge::rhi
