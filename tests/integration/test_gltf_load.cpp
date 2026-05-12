// M25a — glTF + image loader smoke. Stamps a synthetic .glb at a temp path
// (3-vertex triangle, indexed, with normals + UVs) and validates the loader
// returns the expected vertex / index counts and per-vertex values.

#include "mge/assets/gltf_load.h"
#include "mge/assets/texture_load.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

// Synthesize a tiny .glb in memory.
//
//   .glb layout:
//     12-byte header  : magic 'glTF', version 2, total length
//     N JSON chunk    : 8-byte (length, type='JSON'), padded UTF-8 JSON
//     N BIN chunk     : 8-byte (length, type='BIN '), padded binary data
//
// We write three interleaved floats (position xyz) per vertex, plus 3 floats
// of normal, plus 2 floats of UV — 32 bytes/vertex. 3 vertices = 96 bytes.
// Plus 3 uint16 indices = 6 bytes (padded to 4-byte boundary = 8).
struct PackedVertex {
    float pos[3];
    float normal[3];
    float uv[2];
};

std::vector<std::uint8_t> build_synth_glb() {
    // Triangle in the +Y up plane.
    PackedVertex verts[3] = {
        {{-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 1.0f}},
    };
    std::uint16_t indices[3] = {0u, 1u, 2u};

    // Pack bin: 96 bytes vertices + 6 bytes indices = 102 bytes; align to 4.
    std::vector<std::uint8_t> bin;
    bin.resize(sizeof(verts) + sizeof(indices));
    std::memcpy(bin.data(), verts, sizeof(verts));
    std::memcpy(bin.data() + sizeof(verts), indices, sizeof(indices));
    while ((bin.size() % 4) != 0) bin.push_back(0u);

    // JSON references the bin chunk via accessors.
    std::string json = R"({
        "asset":{"version":"2.0"},
        "buffers":[{"byteLength":102}],
        "bufferViews":[
          {"buffer":0,"byteOffset":0,"byteLength":96,"target":34962},
          {"buffer":0,"byteOffset":96,"byteLength":6,"target":34963}
        ],
        "accessors":[
          {"bufferView":0,"byteOffset":0,"componentType":5126,"count":3,"type":"VEC3"},
          {"bufferView":0,"byteOffset":12,"componentType":5126,"count":3,"type":"VEC3"},
          {"bufferView":0,"byteOffset":24,"componentType":5126,"count":3,"type":"VEC2"},
          {"bufferView":1,"byteOffset":0,"componentType":5123,"count":3,"type":"SCALAR"}
        ],
        "materials":[{"name":"M","pbrMetallicRoughness":{"baseColorFactor":[0.8,0.3,0.1,1.0],"metallicFactor":0.0,"roughnessFactor":0.5}}],
        "meshes":[{"name":"tri","primitives":[{
          "attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},
          "indices":3,"material":0,"mode":4
        }]}]
    })";
    // Note: bufferViews must match the actual stride of our packed data.
    // The accessors above describe interleaved attributes via the same
    // bufferView (stride is computed from element size since we don't set
    // `byteStride` — cgltf treats it as tightly packed per attribute, so
    // re-emit per attribute with stride 32. Adjust JSON below.
    json = R"({
        "asset":{"version":"2.0"},
        "buffers":[{"byteLength":102}],
        "bufferViews":[
          {"buffer":0,"byteOffset":0,"byteLength":96,"byteStride":32,"target":34962},
          {"buffer":0,"byteOffset":96,"byteLength":6,"target":34963}
        ],
        "accessors":[
          {"bufferView":0,"byteOffset":0,"componentType":5126,"count":3,"type":"VEC3"},
          {"bufferView":0,"byteOffset":12,"componentType":5126,"count":3,"type":"VEC3"},
          {"bufferView":0,"byteOffset":24,"componentType":5126,"count":3,"type":"VEC2"},
          {"bufferView":1,"byteOffset":0,"componentType":5123,"count":3,"type":"SCALAR"}
        ],
        "materials":[{"name":"M","pbrMetallicRoughness":{"baseColorFactor":[0.8,0.3,0.1,1.0],"metallicFactor":0.0,"roughnessFactor":0.5}}],
        "meshes":[{"name":"tri","primitives":[{
          "attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},
          "indices":3,"material":0,"mode":4
        }]}]
    })";

    // Pad JSON to 4-byte boundary.
    while ((json.size() % 4) != 0) json.push_back(' ');

    const std::uint32_t json_len = static_cast<std::uint32_t>(json.size());
    const std::uint32_t bin_len  = static_cast<std::uint32_t>(bin.size());
    const std::uint32_t total    = 12u + 8u + json_len + 8u + bin_len;

    std::vector<std::uint8_t> glb;
    glb.reserve(total);
    auto push32 = [&](std::uint32_t v) {
        glb.push_back(static_cast<std::uint8_t>(v & 0xFFu));
        glb.push_back(static_cast<std::uint8_t>((v >>  8) & 0xFFu));
        glb.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
        glb.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
    };
    // 12-byte header
    push32(0x46546C67u);            // 'glTF'
    push32(2u);                     // version
    push32(total);                  // total length
    // JSON chunk
    push32(json_len);
    push32(0x4E4F534Au);            // 'JSON'
    glb.insert(glb.end(), json.begin(), json.end());
    // BIN chunk
    push32(bin_len);
    push32(0x004E4942u);            // 'BIN\0'
    glb.insert(glb.end(), bin.begin(), bin.end());
    return glb;
}

}  // namespace

TEST_CASE("gltf loader parses a synthetic single-triangle .glb") {
    const auto glb = build_synth_glb();

    // Write to a temp file because cgltf_parse_file wants a path.
    const auto tmp = std::filesystem::temp_directory_path() / "mge_test_tri.glb";
    {
        std::ofstream out(tmp, std::ios::binary);
        REQUIRE(out.is_open());
        out.write(reinterpret_cast<const char*>(glb.data()),
                  static_cast<std::streamsize>(glb.size()));
    }

    auto scene_opt = mge::assets::load_gltf(tmp.string());
    std::filesystem::remove(tmp);
    REQUIRE(scene_opt.has_value());

    const auto& scene = *scene_opt;
    CHECK(scene.meshes.size() == 1);
    CHECK(scene.materials.size() == 1);

    const auto& mesh = scene.meshes[0];
    CHECK(mesh.vertices.size() == 3);
    CHECK(mesh.indices.size()  == 3);
    CHECK(mesh.material_index == 0u);

    // Vertex 2 = apex at (0,1,0) with UV (0.5, 1.0).
    CHECK(mesh.vertices[2].position.y == doctest::Approx(1.0f));
    CHECK(mesh.vertices[2].uv[0]      == doctest::Approx(0.5f));
    CHECK(mesh.vertices[2].uv[1]      == doctest::Approx(1.0f));

    const auto& mat = scene.materials[0];
    CHECK(mat.name == "M");
    CHECK(mat.base_color_factor[0] == doctest::Approx(0.8f));
    CHECK(mat.metallic_factor      == doctest::Approx(0.0f));
    CHECK(mat.roughness_factor     == doctest::Approx(0.5f));
}
