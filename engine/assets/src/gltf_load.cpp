#include "mge/assets/gltf_load.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace mge::assets {

namespace {

// cgltf returns attribute data through `accessor->data`. Use the helper
// `cgltf_accessor_read_float` so strided / interleaved layouts work without
// manual stride math.
template <std::size_t N>
bool read_floats(const cgltf_accessor* acc, std::size_t element, float (&out)[N]) {
    if (acc == nullptr) return false;
    return cgltf_accessor_read_float(acc, element, out, N);
}

bool read_index(const cgltf_accessor* acc, std::size_t element, std::uint32_t& out) {
    if (acc == nullptr) return false;
    cgltf_uint v = 0;
    if (!cgltf_accessor_read_uint(acc, element, &v, 1)) return false;
    out = static_cast<std::uint32_t>(v);
    return true;
}

[[nodiscard]] std::uint32_t lookup_texture_index(const cgltf_data* data,
                                                  const cgltf_texture* t) {
    if (t == nullptr || t->image == nullptr) return GltfMaterial::npos;
    const auto idx = static_cast<std::uint32_t>(t->image - data->images);
    return idx;
}

DecodedImage decode_gltf_image(const cgltf_data* /*data*/,
                                 const cgltf_image* img,
                                 const std::filesystem::path& base) {
    DecodedImage out;
    if (img == nullptr) return out;

    if (img->uri != nullptr && img->uri[0] != '\0') {
        // External image file — resolve relative to the glTF path.
        std::filesystem::path resolved = base / img->uri;
        return load_image_from_file(resolved.string());
    }
    if (img->buffer_view != nullptr) {
        // Embedded image data inside a buffer view.
        const auto* bv = img->buffer_view;
        const auto* buf_data =
            static_cast<const std::uint8_t*>(bv->buffer->data) + bv->offset;
        return decode_image(std::span<const std::uint8_t>(buf_data, bv->size));
    }
    return out;
}

}  // namespace

std::optional<GltfScene> load_gltf(std::string_view path) noexcept {
    std::string path_str{path};
    cgltf_options options{};
    cgltf_data*   data    = nullptr;
    cgltf_result  result  = cgltf_parse_file(&options, path_str.c_str(), &data);
    if (result != cgltf_result_success) {
        std::fprintf(stderr, "[gltf] parse failed (%d): %s\n",
                     static_cast<int>(result), path_str.c_str());
        return std::nullopt;
    }
    result = cgltf_load_buffers(&options, data, path_str.c_str());
    if (result != cgltf_result_success) {
        std::fprintf(stderr, "[gltf] buffer load failed (%d): %s\n",
                     static_cast<int>(result), path_str.c_str());
        cgltf_free(data);
        return std::nullopt;
    }

    GltfScene scene;
    const std::filesystem::path base = std::filesystem::path(path_str).parent_path();

    // Textures — one entry per glTF image, decoded to RGBA8 in memory.
    scene.textures.reserve(data->images_count);
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        const auto& img = data->images[i];
        GltfTexture t;
        t.name  = img.name ? img.name : ("image_" + std::to_string(i));
        t.image = decode_gltf_image(data, &img, base);
        if (!t.image.valid()) {
            std::fprintf(stderr,
                          "[gltf] image %zu failed to decode (%s)\n",
                          static_cast<std::size_t>(i),
                          img.uri ? img.uri : "<embedded>");
        }
        scene.textures.push_back(std::move(t));
    }

    // Materials.
    scene.materials.reserve(data->materials_count);
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const auto& m = data->materials[i];
        GltfMaterial mat;
        mat.name = m.name ? m.name : ("material_" + std::to_string(i));
        if (m.has_pbr_metallic_roughness) {
            const auto& pbr = m.pbr_metallic_roughness;
            std::memcpy(mat.base_color_factor, pbr.base_color_factor, sizeof(float) * 4);
            mat.metallic_factor       = pbr.metallic_factor;
            mat.roughness_factor      = pbr.roughness_factor;
            mat.base_color_tex        = lookup_texture_index(data, pbr.base_color_texture.texture);
            mat.metallic_roughness_tex= lookup_texture_index(data, pbr.metallic_roughness_texture.texture);
        }
        mat.normal_tex = lookup_texture_index(data, m.normal_texture.texture);
        scene.materials.push_back(std::move(mat));
    }

    // Meshes — flatten primitives into our GltfMesh type. cgltf already
    // exposes accessors per attribute; we read positions / normals / UVs and
    // assemble into GltfVertex per index.
    for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
        const auto& m = data->meshes[mi];
        for (cgltf_size pi = 0; pi < m.primitives_count; ++pi) {
            const auto& prim = m.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;

            GltfMesh out_mesh;
            out_mesh.name = m.name
                ? std::string{m.name} + "/" + std::to_string(pi)
                : "mesh_" + std::to_string(mi) + "/" + std::to_string(pi);
            out_mesh.material_index = prim.material
                ? static_cast<std::uint32_t>(prim.material - data->materials)
                : 0u;

            // Find the position accessor — that drives vertex_count.
            const cgltf_accessor* pos_acc    = nullptr;
            const cgltf_accessor* normal_acc = nullptr;
            const cgltf_accessor* uv_acc     = nullptr;
            for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
                const auto& a = prim.attributes[ai];
                switch (a.type) {
                    case cgltf_attribute_type_position: pos_acc    = a.data; break;
                    case cgltf_attribute_type_normal:   normal_acc = a.data; break;
                    case cgltf_attribute_type_texcoord:
                        if (a.index == 0) uv_acc = a.data;
                        break;
                    default: break;
                }
            }
            if (pos_acc == nullptr) continue;

            const std::size_t vcount = pos_acc->count;
            out_mesh.vertices.resize(vcount);
            for (std::size_t v = 0; v < vcount; ++v) {
                auto& dst = out_mesh.vertices[v];
                float p[3] = {0, 0, 0};
                read_floats(pos_acc, v, p);
                dst.position = math::Vec3{p[0], p[1], p[2]};
                if (normal_acc) {
                    float n[3] = {0, 1, 0};
                    read_floats(normal_acc, v, n);
                    dst.normal = math::Vec3{n[0], n[1], n[2]};
                } else {
                    dst.normal = math::Vec3{0, 1, 0};
                }
                if (uv_acc) {
                    float uv[2] = {0, 0};
                    read_floats(uv_acc, v, uv);
                    dst.uv[0] = uv[0];
                    dst.uv[1] = uv[1];
                } else {
                    dst.uv[0] = 0.0f;
                    dst.uv[1] = 0.0f;
                }
                dst._pad[0] = 0.0f;
                dst._pad[1] = 0.0f;
            }

            // Indices — read as uint32 regardless of source component type.
            if (prim.indices != nullptr) {
                out_mesh.indices.resize(prim.indices->count);
                for (std::size_t i = 0; i < prim.indices->count; ++i) {
                    read_index(prim.indices, i, out_mesh.indices[i]);
                }
            } else {
                // Non-indexed primitive — synthesise a 0..N range.
                out_mesh.indices.resize(vcount);
                for (std::size_t i = 0; i < vcount; ++i) {
                    out_mesh.indices[i] = static_cast<std::uint32_t>(i);
                }
            }

            scene.meshes.push_back(std::move(out_mesh));
        }
    }

    cgltf_free(data);
    return scene;
}

}  // namespace mge::assets
