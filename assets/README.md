# Demo assets

Local-only — `assets/gltf/` is gitignored (binary models + textures don't belong in source control).

## Fetching the demo models

The hello_metal demo accepts `--gltf <path>` to swap the M25b procedural cube
for a real glTF mesh. The expected reference asset is Khronos's DamagedHelmet
(classic PBR validation model: base color + normal + metallic-roughness + AO +
emissive, all packed in a single `.glb`).

```sh
mkdir -p assets/gltf
curl -sLfo assets/gltf/DamagedHelmet.glb \
  https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/DamagedHelmet/glTF-Binary/DamagedHelmet.glb
```

Then:

```sh
./build/debug/examples/hello_metal/hello_metal --gltf assets/gltf/DamagedHelmet.glb
```

## Other Khronos samples worth pointing at

Same URL pattern, swap the model name. All `glTF-Binary/` directories ship a
single `.glb` so cgltf's URI resolver doesn't need to chase external buffers.

- `BoomBox` — small, clean PBR
- `Avocado` — single-mesh, single-material
- `WaterBottle` — transparency stress
- `FlightHelmet` — multi-mesh, multi-material (also has the only `.gltf`-only
  variant — that one would need its `.bin` + texture siblings copied too)

## Provenance

Khronos glTF Sample Assets are CC-BY 4.0; see the upstream
[LICENSE.md](https://github.com/KhronosGroup/glTF-Sample-Assets/blob/main/LICENSE.md).
