# Real-Time Rendering 4e — Chapter 9: Physically Based Shading

> Notes for our M6 PBR implementation. Source is RTR4 Ch. 9 + supplemental from RTR4 Ch. 5 / 8.

## Core ideas we will implement

### Microfacet BRDF

The standard real-time PBR BRDF is `f = (D · F · G) / (4 · cos θᵢ · cos θₒ) + diffuse`:

- **D — Normal distribution function**: how microfacets aim. We use **GGX** (Trowbridge-Reitz) with roughness² as the conventional parameter.
- **F — Fresnel**: reflectance at grazing angles. We use **Schlick's approximation**, parameterized by F0 (3-channel for metals, single-grey for dielectrics).
- **G / V — Geometry / visibility**: shadowing-masking. We use **Smith correlated visibility**, which is `D · F · V` formulation that pre-divides by 4·cos·cos.

Diffuse term: **Lambert** for Phase 1. Optional **Disney diffuse** later (Phase 1.5).

### Metallic-roughness workflow

Authoring uses two scalar maps + a base color:

- **Base color** (RGB): for non-metals, this is diffuse albedo. For metals, it is the F0 reflectance.
- **Metallic** (0..1): selects between dielectric (F0=0.04) and metal (F0=base_color).
- **Roughness** (0..1): perceptual roughness, squared to obtain GGX α.

Storage in G-Buffer:
- Albedo (3 ch) + AO (1 ch) in slot 0 RGBA8.
- Normal (octahedral 2×16) + Roughness (8) + Metallic (8) in slot 1.
- F0 derived from base_color + metallic in lighting (no need to store separately if metallic is stored).

### Energy conservation & multi-scattering

The classic single-bounce GGX loses energy at high roughness. Real surfaces re-scatter that lost energy. Two fixes:

- **Fdez-Agüera 2019** — analytic compensation, no precomputed tables. Simple and free. **Use this.**
- **Heitz multiscatter LUT** — precomputed tables. More accurate, more memory.

Validation: white-furnace test. A scene with constant white environment should output ~1.0 for all roughnesses. Phase 1 furnace golden test asserts ≤ 2 % energy loss at every (roughness, NdotV) sample. See M6.

### IBL — Image-Based Lighting

Two precomputed pieces (Karis 2013 split-sum):

1. **Irradiance map** — diffuse component. Cosine-weighted average of the environment, stored at low resolution (8x8x6 to 16x16x6).
2. **Pre-filtered specular** — convolved with the GGX kernel at multiple roughnesses, stored as a mip chain. Mip 0 = mirror, mip N = fully rough.
3. **BRDF LUT** — 2D RG texture indexed by (NdotV, roughness), encoding the integral of F·G over the hemisphere.

Final indirect specular: `prefilteredColor(R, roughness_mip) * (F0 * brdf_lut.r + brdf_lut.g)`.

We need an offline tool (M6 sub-deliverable) to bake these from an equirectangular HDR.

### Fresnel notes

Schlick's approximation: `F = F0 + (1 - F0)·(1 - cos θ)⁵`.

- F0 dielectric ≈ 0.04 (4 % at normal incidence).
- F0 metallic = base color (chromatic; gold ≈ (1.0, 0.71, 0.29)).
- At grazing angles all surfaces approach white. This dominates rim lighting.

### Normal mapping & tangent space

We use **MikkTSpace** for tangent space generation (`mikktspace` lib, M6+). Anything else risks mismatched normal-map authoring tools.

## Common pitfalls (RTR4 + experience)

1. **sRGB vs linear** — albedo / base color textures are sRGB; everything else (normals, roughness, metallic, AO) is linear. Apply texture views correctly.
2. **Energy loss at high roughness** without multi-scattering compensation → white materials look gray.
3. **Mip selection on prefiltered map** — clamp to last mip to avoid undefined samples at roughness=1.
4. **GGX α ↔ roughness** confusion: we use perceptual roughness in code, square at the shader entry. Don't mix.
5. **Reverse-Z + linear-Z** — depth precision matters for screen-space lighting. Always reverse-Z.

## What we ship at M6

- GGX-Smith with multiscatter compensation.
- Lambert diffuse.
- Schlick Fresnel.
- Split-sum IBL with offline-baked irradiance + specular + BRDF LUT.
- Furnace golden test.
- Reference scene golden (cube of materials × roughness × metallic).

## See also

- RTR4 Ch. 8 (Light) — color spaces, tonemapping.
- Karis 2013 "Real Shading in Unreal Engine 4" — split-sum derivation.
- Frostbite 2014 "Moving Frostbite to PBR".
- Fdez-Agüera 2019 "A Multiple-Scattering Microfacet Model for Real-Time Image-Based Lighting".
