#pragma once

// M25a — minimal glTF 2.0 scene loader. Built on cgltf. Returns a flat,
// engine-friendly representation: a list of meshes (each with vertices +
// indices) and a list of materials referencing texture indices. Phase 2.b
// extensions (skinning, animation channels, full PBR spec) layer on top of
// this without changing the public types.

#include "mge/assets/pbr_mesh.h"
#include "mge/assets/texture_load.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mge::assets {

// glTF 2.0 vertex enriched with UV — what the textured PBR shader will
// consume. Lays out as 48 bytes (16-byte aligned position + normal + 8-byte
// UV + 8-byte tangent placeholder).
struct GltfVertex {
    math::Vec3 position;     // 16 B
    math::Vec3 normal;       // 16 B
    float      uv[2];        //  8 B
    float      _pad[2];      //  8 B (room for tangent.xy in a future pass)
};
static_assert(sizeof(GltfVertex) == 48);

struct GltfMesh {
    std::string                 name;
    std::vector<GltfVertex>     vertices;
    std::vector<std::uint32_t>  indices;
    std::uint32_t               material_index = 0;   // index into GltfScene::materials
};

struct GltfMaterial {
    std::string name;

    // Constant factors — applied whether or not the maps are present.
    float base_color_factor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float metallic_factor      = 1.0f;
    float roughness_factor     = 1.0f;

    // Indices into GltfScene::textures (npos = absent).
    static constexpr std::uint32_t npos = ~std::uint32_t{0};
    std::uint32_t base_color_tex          = npos;
    std::uint32_t normal_tex              = npos;
    std::uint32_t metallic_roughness_tex  = npos;
};

struct GltfTexture {
    std::string  name;
    DecodedImage image;        // owns the pixel data
};

struct GltfScene {
    std::vector<GltfMesh>     meshes;
    std::vector<GltfMaterial> materials;
    std::vector<GltfTexture>  textures;
};

// Load a `.gltf` or `.glb` file from disk. `path` is the path to the
// container; cgltf resolves URIs (.bin buffers, image files) relative to it.
// Returns std::nullopt on parse / IO failure; check the log on stderr.
[[nodiscard]] std::optional<GltfScene> load_gltf(std::string_view path) noexcept;

}  // namespace mge::assets
