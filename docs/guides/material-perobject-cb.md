<!-- doc-meta
title: PerObjectCB Layout and Material Flags
category: rendering
theme: material-system
status: done
last-updated: 2026-03-24
-->

# PerObjectCB Layout and Material Flags

This guide documents the complete layout of `PerObjectCB` — the per-draw-call constant buffer that carries all material and transform data to the GPU. It includes the precise byte offsets, field purposes, and the compact `materialFlags` bitfield encoding used to pack multiple material parameters into 32 bits.

## PerObjectCB Overview

`PerObjectCB` is a **256-byte constant buffer** bound at `b0` (register slot 0) in both vertex and pixel shaders. It's uploaded once per draw call and contains:

- Transform matrices (model, normal matrix)
- Texture SRV heap indices (8 slots)
- Scalar material parameters (tints, multipliers, thresholds)
- POM (Parallax Occlusion Mapping) parameters
- Emissive parameters
- Compact `materialFlags` bitfield

The buffer size is fixed at 256 bytes to align with D3D12 constant buffer granularity (256-byte aligned for optimal GPU memory).

## Complete PerObjectCB Field Layout

```
Offset (bytes)   Field                      Type         Size   Description
──────────────────────────────────────────────────────────────────────────────────
0                albedoIndex                uint32       4      SRV heap index for BaseColor (t0)
4                normalIndex                uint32       4      SRV heap index for normal (t1)
8                roughnessIndex             uint32       4      SRV heap index for roughness (t2)
12               metallicIndex              uint32       4      SRV heap index for metallic (t3)
16               aoIndex                    uint32       4      SRV heap index for AO (t4)
20               emissiveIndex              uint32       4      SRV heap index for emissive (t5)
24               alphaClipThreshold         float        4      Alpha test threshold (default 0.3333)
28               opacitySrvIndex            uint32       4      SRV heap index for opacity/alpha (t6)
──────────────────────────────────────────────────────────────────────────────────
32               model[0]                   float4       16     Object-to-world transform row 0
48               model[1]                   float4       16     Object-to-world transform row 1
64               model[2]                   float4       16     Object-to-world transform row 2
80               model[3]                   float4       16     Object-to-world transform row 3
──────────────────────────────────────────────────────────────────────────────────
96               normalMatrix[0]            float4       16     transpose(inverse(model)) row 0
112              normalMatrix[1]            float4       16     transpose(inverse(model)) row 1
128              normalMatrix[2]            float4       16     transpose(inverse(model)) row 2
144              normalMatrix[3]            float4       16     transpose(inverse(model)) row 3
──────────────────────────────────────────────────────────────────────────────────
160              emissiveColorR             float        4      Emissive tint R (linear, default 0)
164              emissiveColorG             float        4      Emissive tint G (linear, default 0)
168              emissiveColorB             float        4      Emissive tint B (linear, default 0)
172              emissiveIntensity          float        4      Emissive brightness (default 0)
──────────────────────────────────────────────────────────────────────────────────
176              baseColorTintR             float        4      Albedo multiplier R (default 1.0)
180              baseColorTintG             float        4      Albedo multiplier G (default 1.0)
184              baseColorTintB             float        4      Albedo multiplier B (default 1.0)
188              roughnessMultiplier        float        4      Roughness scale (default 1.0)
192              metallicMultiplier         float        4      Metallic scale (default 1.0)
196              normalIntensity            float        4      Normal map strength (default 1.0)
200              uvScaleX                   float        4      Texture tiling X (default 1.0)
204              uvScaleY                   float        4      Texture tiling Y (default 1.0)
208              uvOffsetX                  float        4      Texture offset X (default 0.0)
212              uvOffsetY                  float        4      Texture offset Y (default 0.0)
216              aoIntensity                float        4      AO strength (default 1.0)
220              opacity                    float        4      Material opacity (default 1.0)
──────────────────────────────────────────────────────────────────────────────────
224              displacementScale          float        4      POM height scale (default 0.0)
228              heightmapSrvIndex          uint32       4      SRV heap index for heightmap
232              pomMinLayers               float        4      Min ray march steps (default 8)
236              pomMaxLayers               float        4      Max ray march steps (default 32)
240              pomFadeStart               float        4      POM fade distance start (default 1000)
244              pomFadeEnd                 float        4      POM fade distance end (default 2000)
248              pomSelfShadow              float        4      Self-shadow strength (default 0)
252              materialFlags              uint32       4      Compact bitfield (blend, ORM, etc.)
──────────────────────────────────────────────────────────────────────────────────
                                           TOTAL        256    (256-byte aligned)
```

## Texture SRV Index Fields (Offsets 0–28)

The first 32 bytes contain SRV heap indices for the 8 standard material texture slots:

```cpp
uint32_t albedoIndex;      // offset 0
uint32_t normalIndex;      // offset 4
uint32_t roughnessIndex;   // offset 8
uint32_t metallicIndex;    // offset 12
uint32_t aoIndex;          // offset 16
uint32_t emissiveIndex;    // offset 20
float    alphaClipThreshold; // offset 24
uint32_t opacitySrvIndex;  // offset 28
```

| Field | Offset | Purpose | SRV Register | Default (0 value) |
|-------|--------|---------|--------------|-------------------|
| `albedoIndex` | 0 | BaseColor texture | `t0` | White (1, 1, 1) |
| `normalIndex` | 4 | Tangent-space normal | `t1` | Flat (0.5, 0.5, 1.0) |
| `roughnessIndex` | 8 | Surface roughness | `t2` | White (1.0) |
| `metallicIndex` | 12 | Metallic reflectance | `t3` | Black (0.0) |
| `aoIndex` | 16 | Ambient occlusion | `t4` | White (1.0) |
| `emissiveIndex` | 20 | Self-illumination | `t5` | Black (0, 0, 0) |
| `opacitySrvIndex` | 28 | Alpha mask/opacity | `t6` | White (1.0) |

When an index is `0`, the shader uses a hardcoded default texture instead of heap lookup. This allows efficient fallback without allocating textures for missing slots.

## Transform Fields (Offsets 32–159)

Two 4x4 matrices for transforming world and normal coordinates:

```cpp
Mat4 model;         // offset 32, size 64 bytes
Mat4 normalMatrix;  // offset 96, size 64 bytes
```

### model matrix

Object-to-world transform. Stored row-major (each row is a float4).

```
Position in world: worldPos = model * localPos
```

### normalMatrix

`transpose(inverse(model))` — used for transforming normal vectors.

```
Normal in world: worldNormal = normalMatrix * localNormal
```

Precomputed on CPU to avoid per-pixel matrix inversion. Critical for correct lighting when the model matrix includes non-uniform scale.

## Emissive Parameters (Offsets 160–175)

```cpp
float emissiveColorR = 0.0f;  // offset 160
float emissiveColorG = 0.0f;  // offset 164
float emissiveColorB = 0.0f;  // offset 168
float emissiveIntensity = 0.0f; // offset 172
```

Emissive output is computed as:

```glsl
float3 emissive = emissiveColor * emissiveIntensity;
// Added to final pixel (bypasses lighting)
```

Default (0, 0, 0, 0) = no emission. Values > 1.0 for intensity drive bloom effects.

## Material Parameters (Offsets 176–220)

Scalar multipliers and parameters that modify or control texture-sampled values:

```cpp
float baseColorTintR = 1.0f;    // offset 176, albedo RGB multiplier
float baseColorTintG = 1.0f;    // offset 180
float baseColorTintB = 1.0f;    // offset 184
float roughnessMultiplier = 1.0f; // offset 188, scales roughness sample
float metallicMultiplier = 1.0f; // offset 192, scales metallic sample
float normalIntensity = 1.0f;   // offset 196, normal map perturbation strength
float uvScaleX = 1.0f;          // offset 200, texture tiling X
float uvScaleY = 1.0f;          // offset 204, texture tiling Y
float uvOffsetX = 0.0f;         // offset 208, texture offset X
float uvOffsetY = 0.0f;         // offset 212, texture offset Y
float aoIntensity = 1.0f;       // offset 216, lerp(1.0, ao_tex, intensity)
float opacity = 1.0f;           // offset 220, overall material opacity
```

All parameters use **neutral defaults** (1.0 for multipliers, 0.0 for offsets) so an unmodified material produces the same visual result as the existing hardcoded pipeline.

## POM Parameters (Offsets 224–252)

Parallax Occlusion Mapping displacement and quality tuning:

```cpp
float displacementScale = 0.0f;  // offset 224, height scale (0=disabled)
uint32_t heightmapSrvIndex = 0;  // offset 228, SRV heap index for heightmap
float pomMinLayers = 8.0f;       // offset 232, min ray march steps
float pomMaxLayers = 32.0f;      // offset 236, max ray march steps
float pomFadeStart = 1000.0f;    // offset 240, fade distance start (cm)
float pomFadeEnd = 2000.0f;      // offset 244, fade distance end (cm)
float pomSelfShadow = 0.0f;      // offset 248, self-shadow strength (0-1)
```

When `displacementScale = 0.0f`, POM is disabled and `heightmapSrvIndex` is ignored. Setting `displacementScale > 0` enables the POM effect.

See `material-texture-slots.md` for heightmap details.

## materialFlags Bitfield (Offset 252)

The final 4 bytes encode multiple material parameters into a compact 32-bit bitfield to save CB space:

```cpp
uint32_t materialFlags = 0;
```

### Bitfield Layout

```
Bit Range    Field                     Encoding         Range / Meaning
──────────────────────────────────────────────────────────────────────
[1:0]        blendMode                 2 bits           0=Opaque, 1=Masked, 2=Translucent, 3=Additive
[2]          useORM                    1 bit            0=separate R/M/AO, 1=packed ORM
[3]          twoSided                  1 bit            0=back-face cull, 1=no cull (shader-ignores; PSO level)
[8:15]       specular * 255            8 bits           0.0–1.0 range, 255 = 1.0
[16:23]      envMultiplier * 127.5     8 bits           0.0–2.0 range, 128 = 1.0
[24:27]      shadingModelID            4 bits           0–15 (EShadingModel enum)
[28:31]      clearCoatRoughness * 15   4 bits           0.0–1.0 range (16 levels)
```

### Detailed Field Descriptions

#### blendMode (bits 1:0)

```
value = (materialFlags >> 0) & 0x3u;

0 = Opaque       — deferred, no blending
1 = Masked       — deferred, alpha test via discard
2 = Translucent  — forward, SrcAlpha/InvSrcAlpha blend
3 = Additive     — forward, One/One additive blend
```

Used in geometry pass to route to correct PSO:
- If blendMode < 2: render in deferred G-Buffer pass
- If blendMode >= 2: defer to forward pass (post-lighting)

#### useORM (bit 2)

```
value = (materialFlags >> 2) & 0x1u;

0 = separate textures (roughnessIndex, metallicIndex, aoIndex)
1 = packed ORM (aoIndex reinterpreted as ORM with R=AO, G=Rough, B=Metal)
```

#### twoSided (bit 3)

```
value = (materialFlags >> 3) & 0x1u;

0 = back-face cull (CullMode::Back)
1 = no cull (CullMode::None)
```

**Note**: This bit is informational for the shader. The actual cull mode is determined by the PSO used (GetGBuffer vs. GetGBufferMasked). The bit allows the shader to know whether back-facing pixels are present.

#### specular (bits 15:8)

Dielectric specular reflectance scale controlling F0 for non-metallic surfaces.

```
byte_value = round(specular * 255);
specular_float = byte_value / 255.0;  // 0.0–1.0

F0 = 0.08 * specular_float;  // e.g., 0.5 → F0=0.04 (IOR ~1.5)
```

Extraction in shader:
```glsl
float specular = ((materialFlags >> 8) & 0xFFu) / 255.0f;
```

Default packing: `specular=0.5f` → byte=127 → `(127 << 8) | ...`

#### envMultiplier (bits 23:16)

Per-material IBL intensity multiplier. Controls how much environment lighting (skylight, reflections) affects this surface.

```
byte_value = round(envMultiplier * 127.5);
envMultiplier_float = byte_value / 127.5;  // 0.0–2.0 range

Output IBL = IBL * envMultiplier_float;
```

Key value: `128 = 1.0` (full contribution)

Extraction in shader:
```glsl
float envMult = ((materialFlags >> 16) & 0xFFu) / 127.5f;
```

Default: `envMultiplier=1.0f` → byte=128

#### shadingModelID (bits 27:24)

Shading model selector for which BRDF is evaluated in the lighting pass.

```
value = (materialFlags >> 24) & 0xFu;  // 4-bit, 0–15

0 = DefaultLit       (Cook-Torrance GGX PBR)
1 = Subsurface       (SSS + Burley diffuse)
2 = ClearCoat        (two-layer specular)
3 = Cloth            (fabric sheen BRDF)
4 = Unlit            (no lighting, emissive only)
... (15 total possible)
```

### clearCoatRoughness (bits 31:28)

Clear coat layer roughness (only used when shadingModelID == ClearCoat).

```
nibble_value = round(clearCoatRoughness * 15.0);
clearCoatRoughness_float = nibble_value / 15.0;  // 0.0–1.0 via 16 levels

Extraction in shader:
float ccRough = ((materialFlags >> 28) & 0xFu) / 15.0f;
```

Default: `clearCoatRoughness=0.04f` → nibble≈1 (near-mirror coating)

## Bit Packing Example

Suppose we have:
- `blendMode = 0` (Opaque)
- `useORM = 0` (separate textures)
- `twoSided = 0` (cull back faces)
- `specular = 0.5f` → byte = 127
- `envMultiplier = 1.0f` → byte = 128
- `shadingModelID = 0` (DefaultLit)
- `clearCoatRoughness = 0.04f` → nibble = 1

Assembly:
```
Bits:  [31:28]  [27:24]  [23:16]  [15:8]   [7:4]   [3]   [2]   [1:0]
Value:    0001     0000   10000000 01111111  (unused) 0     0     00

materialFlags = 0x10807F00
              = (1 << 28) | (0 << 24) | (128 << 16) | (127 << 8) | 0
```

## Data Flow: Material → CB

The flow from `Material` asset to `PerObjectCB` in the GPU:

```
1. Scene renderer iterates draw commands
2. For each draw: reads Material/MaterialInstance
3. Extracts parameters:
   - Texture SRV indices (loaded by renderer)
   - Scalar tints/multipliers
   - Blend mode, shading model, etc.
4. Packs into materialFlags bitfield
5. Populates PerObjectCB struct on CPU
6. Uploads CB to GPU via constant buffer ring
7. Shader reads CB at b0, unpacks fields
8. Pixel shader uses values in lighting equation
```

### Example Code

```cpp
// In scene renderer's draw command builder
PerObjectCB cb;

// Texture indices
cb.albedoIndex = material->GetTextureSrvIndex("Albedo");
cb.normalIndex = material->GetTextureSrvIndex("Normal");
// ... etc for other slots

// Matrices
cb.model = transform.GetMatrix();
cb.normalMatrix = transform.GetNormalMatrix();

// Scalar parameters
cb.baseColorTintR = material->GetScalarParam("BaseColorTintR", 1.0f);
cb.baseColorTintG = material->GetScalarParam("BaseColorTintG", 1.0f);
cb.baseColorTintB = material->GetScalarParam("BaseColorTintB", 1.0f);
cb.roughnessMultiplier = material->GetRoughnessMultiplier();
// ... etc

// Pack materialFlags
uint32_t blendMode = (uint32_t)material->GetBlendMode();
uint32_t shadingModel = (uint32_t)material->GetShadingModel();
uint32_t specularByte = (uint32_t)(material->GetSpecular() * 255.0f);
uint32_t envByte = (uint32_t)(material->GetEnvMultiplier() * 127.5f);
uint32_t twoSided = material->IsTwoSided() ? 1 : 0;
uint32_t useORM = material->IsORM() ? 1 : 0;

cb.materialFlags = (blendMode << 0) |
                   (useORM << 2) |
                   (twoSided << 3) |
                   (specularByte << 8) |
                   (envByte << 16) |
                   (shadingModel << 24);

// Upload to GPU
constantBufferRing->Allocate(sizeof(PerObjectCB), (void*)&cb);
```

## Summary

`PerObjectCB` is a 256-byte, per-draw-call constant buffer carrying all material and transform data. It includes:

- **Texture indices** (8 slots) for material textures
- **Transforms** (2 matrices) for world and normal space
- **Scalar parameters** (11 floats) for tints and multipliers
- **POM parameters** (7 floats/uints) for displacement
- **materialFlags** (1 uint32) — compact 32-bit bitfield encoding blend mode, shading model, specular, IBL intensity, and clearcoat roughness

The bitfield packing reduces CB space by ~32 bytes, allowing the full material definition to fit within the tight 256-byte D3D12 constant buffer granule while maintaining performance.
