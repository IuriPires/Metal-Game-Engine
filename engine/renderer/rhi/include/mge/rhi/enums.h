#pragma once

#include <cstdint>

namespace mge::rhi {

enum class PixelFormat : std::uint16_t {
    Undefined = 0,
    R8Unorm,
    RG8Unorm,
    RGBA8Unorm,
    RGBA8UnormSrgb,
    BGRA8Unorm,
    BGRA8UnormSrgb,
    R16Float,
    RG16Float,
    RGBA16Float,
    R32Float,
    RG32Float,
    RGBA32Float,
    Depth32Float,
    Depth24UnormStencil8,
};

enum class LoadAction : std::uint8_t {
    DontCare = 0,
    Load,
    Clear,
};

enum class StoreAction : std::uint8_t {
    DontCare = 0,
    Store,
    MultisampleResolve,
};

enum class PrimitiveTopology : std::uint8_t {
    PointList = 0,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip,
};

enum class IndexType : std::uint8_t {
    UInt16 = 0,
    UInt32,
};

enum class StorageMode : std::uint8_t {
    // GPU-only. CPU access via blits. Best for static GPU data.
    Private = 0,
    // CPU and GPU share memory (unified-memory devices). Default on Apple Silicon.
    Shared,
    // CPU has its own copy; explicit synchronize call to flush to GPU. Mac with discrete GPU.
    Managed,
    // No backing store (transient tile memory only). Apple GPU tile attachments.
    Memoryless,
};

enum class BufferUsage : std::uint32_t {
    None     = 0,
    Vertex   = 1u << 0,
    Index    = 1u << 1,
    Uniform  = 1u << 2,
    Storage  = 1u << 3,
    CopySrc  = 1u << 4,
    CopyDst  = 1u << 5,
};

constexpr BufferUsage operator|(BufferUsage a, BufferUsage b) noexcept {
    return BufferUsage(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

constexpr bool any(BufferUsage a, BufferUsage b) noexcept {
    return (static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b)) != 0;
}

enum class TextureUsage : std::uint32_t {
    None         = 0,
    ShaderRead   = 1u << 0,
    ShaderWrite  = 1u << 1,
    RenderTarget = 1u << 2,
    CopySrc      = 1u << 3,
    CopyDst      = 1u << 4,
};

constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) noexcept {
    return TextureUsage(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

constexpr bool any(TextureUsage a, TextureUsage b) noexcept {
    return (static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b)) != 0;
}

enum class VertexFormat : std::uint8_t {
    Float32   = 0,
    Float32x2,
    Float32x3,
    Float32x4,
    UInt32,
    UInt32x2,
    UInt32x4,
    UByte4Norm,
    Short2Norm,
};

enum class ShaderStage : std::uint8_t {
    Vertex   = 0,
    Fragment,
    Compute,
};

[[nodiscard]] inline std::uint32_t bytes_of(VertexFormat f) noexcept {
    switch (f) {
        case VertexFormat::Float32:    return 4;
        case VertexFormat::Float32x2:  return 8;
        case VertexFormat::Float32x3:  return 12;
        case VertexFormat::Float32x4:  return 16;
        case VertexFormat::UInt32:     return 4;
        case VertexFormat::UInt32x2:   return 8;
        case VertexFormat::UInt32x4:   return 16;
        case VertexFormat::UByte4Norm: return 4;
        case VertexFormat::Short2Norm: return 4;
    }
    return 0;
}

}  // namespace mge::rhi
