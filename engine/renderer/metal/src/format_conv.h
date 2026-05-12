#pragma once

// Internal conversion helpers between mge::rhi enums and Metal types.
// Included only by Metal backend .cpp files.

#include "mge/renderer/metal/metal_cpp.h"
#include "mge/rhi/enums.h"
#include "mge/rhi/sampler.h"

namespace mge::rhi::metal_backend {

[[nodiscard]] inline MTL::PixelFormat to_mtl(PixelFormat f) noexcept {
    switch (f) {
        case PixelFormat::Undefined:           return MTL::PixelFormatInvalid;
        case PixelFormat::R8Unorm:             return MTL::PixelFormatR8Unorm;
        case PixelFormat::RG8Unorm:            return MTL::PixelFormatRG8Unorm;
        case PixelFormat::RGBA8Unorm:          return MTL::PixelFormatRGBA8Unorm;
        case PixelFormat::RGBA8UnormSrgb:      return MTL::PixelFormatRGBA8Unorm_sRGB;
        case PixelFormat::BGRA8Unorm:          return MTL::PixelFormatBGRA8Unorm;
        case PixelFormat::BGRA8UnormSrgb:      return MTL::PixelFormatBGRA8Unorm_sRGB;
        case PixelFormat::R16Float:            return MTL::PixelFormatR16Float;
        case PixelFormat::RG16Float:           return MTL::PixelFormatRG16Float;
        case PixelFormat::RGBA16Float:         return MTL::PixelFormatRGBA16Float;
        case PixelFormat::R32Float:            return MTL::PixelFormatR32Float;
        case PixelFormat::RG32Float:           return MTL::PixelFormatRG32Float;
        case PixelFormat::RGBA32Float:         return MTL::PixelFormatRGBA32Float;
        case PixelFormat::Depth32Float:        return MTL::PixelFormatDepth32Float;
        case PixelFormat::Depth24UnormStencil8: return MTL::PixelFormatDepth24Unorm_Stencil8;
    }
    return MTL::PixelFormatInvalid;
}

[[nodiscard]] inline MTL::StorageMode to_mtl(StorageMode m) noexcept {
    switch (m) {
        case StorageMode::Private:    return MTL::StorageModePrivate;
        case StorageMode::Shared:     return MTL::StorageModeShared;
        case StorageMode::Managed:    return MTL::StorageModeManaged;
        case StorageMode::Memoryless: return MTL::StorageModeMemoryless;
    }
    return MTL::StorageModeShared;
}

[[nodiscard]] inline MTL::ResourceOptions resource_options_for(StorageMode m) noexcept {
    switch (m) {
        case StorageMode::Private:    return MTL::ResourceStorageModePrivate;
        case StorageMode::Shared:     return MTL::ResourceStorageModeShared;
        case StorageMode::Managed:    return MTL::ResourceStorageModeManaged;
        case StorageMode::Memoryless: return MTL::ResourceStorageModeMemoryless;
    }
    return MTL::ResourceStorageModeShared;
}

[[nodiscard]] inline MTL::TextureUsage to_mtl(TextureUsage u) noexcept {
    MTL::TextureUsage result = MTL::TextureUsageUnknown;
    if (any(u, TextureUsage::ShaderRead))   result |= MTL::TextureUsageShaderRead;
    if (any(u, TextureUsage::ShaderWrite))  result |= MTL::TextureUsageShaderWrite;
    if (any(u, TextureUsage::RenderTarget)) result |= MTL::TextureUsageRenderTarget;
    return result;
}

[[nodiscard]] inline MTL::LoadAction to_mtl(LoadAction a) noexcept {
    switch (a) {
        case LoadAction::DontCare: return MTL::LoadActionDontCare;
        case LoadAction::Load:     return MTL::LoadActionLoad;
        case LoadAction::Clear:    return MTL::LoadActionClear;
    }
    return MTL::LoadActionDontCare;
}

[[nodiscard]] inline MTL::StoreAction to_mtl(StoreAction a) noexcept {
    switch (a) {
        case StoreAction::DontCare:            return MTL::StoreActionDontCare;
        case StoreAction::Store:               return MTL::StoreActionStore;
        case StoreAction::MultisampleResolve:  return MTL::StoreActionMultisampleResolve;
    }
    return MTL::StoreActionStore;
}

[[nodiscard]] inline MTL::PrimitiveType to_mtl(PrimitiveTopology t) noexcept {
    switch (t) {
        case PrimitiveTopology::PointList:     return MTL::PrimitiveTypePoint;
        case PrimitiveTopology::LineList:      return MTL::PrimitiveTypeLine;
        case PrimitiveTopology::LineStrip:     return MTL::PrimitiveTypeLineStrip;
        case PrimitiveTopology::TriangleList:  return MTL::PrimitiveTypeTriangle;
        case PrimitiveTopology::TriangleStrip: return MTL::PrimitiveTypeTriangleStrip;
    }
    return MTL::PrimitiveTypeTriangle;
}

[[nodiscard]] inline MTL::CompareFunction to_mtl(DepthCompare c) noexcept {
    switch (c) {
        case DepthCompare::Never:        return MTL::CompareFunctionNever;
        case DepthCompare::Less:         return MTL::CompareFunctionLess;
        case DepthCompare::Equal:        return MTL::CompareFunctionEqual;
        case DepthCompare::LessEqual:    return MTL::CompareFunctionLessEqual;
        case DepthCompare::Greater:      return MTL::CompareFunctionGreater;
        case DepthCompare::NotEqual:     return MTL::CompareFunctionNotEqual;
        case DepthCompare::GreaterEqual: return MTL::CompareFunctionGreaterEqual;
        case DepthCompare::Always:       return MTL::CompareFunctionAlways;
    }
    return MTL::CompareFunctionAlways;
}

[[nodiscard]] inline MTL::CullMode to_mtl(CullMode c) noexcept {
    switch (c) {
        case CullMode::None:  return MTL::CullModeNone;
        case CullMode::Front: return MTL::CullModeFront;
        case CullMode::Back:  return MTL::CullModeBack;
    }
    return MTL::CullModeNone;
}

[[nodiscard]] inline MTL::Winding to_mtl(FrontFace f) noexcept {
    return f == FrontFace::Clockwise ? MTL::WindingClockwise : MTL::WindingCounterClockwise;
}

[[nodiscard]] inline MTL::IndexType to_mtl(IndexType t) noexcept {
    return t == IndexType::UInt16 ? MTL::IndexTypeUInt16 : MTL::IndexTypeUInt32;
}

[[nodiscard]] inline MTL::SamplerMinMagFilter to_mtl(FilterMode f) noexcept {
    return f == FilterMode::Linear ? MTL::SamplerMinMagFilterLinear
                                   : MTL::SamplerMinMagFilterNearest;
}

[[nodiscard]] inline MTL::SamplerMipFilter to_mtl(MipFilter f) noexcept {
    switch (f) {
        case MipFilter::NotMipmapped: return MTL::SamplerMipFilterNotMipmapped;
        case MipFilter::Nearest:      return MTL::SamplerMipFilterNearest;
        case MipFilter::Linear:       return MTL::SamplerMipFilterLinear;
    }
    return MTL::SamplerMipFilterNotMipmapped;
}

[[nodiscard]] inline MTL::SamplerAddressMode to_mtl(AddressMode a) noexcept {
    switch (a) {
        case AddressMode::Repeat:       return MTL::SamplerAddressModeRepeat;
        case AddressMode::MirrorRepeat: return MTL::SamplerAddressModeMirrorRepeat;
        case AddressMode::ClampToEdge:  return MTL::SamplerAddressModeClampToEdge;
        case AddressMode::ClampToZero:  return MTL::SamplerAddressModeClampToZero;
    }
    return MTL::SamplerAddressModeClampToEdge;
}

[[nodiscard]] inline MTL::VertexFormat to_mtl(VertexFormat f) noexcept {
    switch (f) {
        case VertexFormat::Float32:    return MTL::VertexFormatFloat;
        case VertexFormat::Float32x2:  return MTL::VertexFormatFloat2;
        case VertexFormat::Float32x3:  return MTL::VertexFormatFloat3;
        case VertexFormat::Float32x4:  return MTL::VertexFormatFloat4;
        case VertexFormat::UInt32:     return MTL::VertexFormatUInt;
        case VertexFormat::UInt32x2:   return MTL::VertexFormatUInt2;
        case VertexFormat::UInt32x4:   return MTL::VertexFormatUInt4;
        case VertexFormat::UByte4Norm: return MTL::VertexFormatUChar4Normalized;
        case VertexFormat::Short2Norm: return MTL::VertexFormatShort2Normalized;
    }
    return MTL::VertexFormatInvalid;
}

[[nodiscard]] inline NS::String* ns_str(const std::string& s) noexcept {
    return NS::String::string(s.c_str(), NS::UTF8StringEncoding);
}

}  // namespace mge::rhi::metal_backend
