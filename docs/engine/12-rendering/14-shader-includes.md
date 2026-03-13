# Shared HLSL Include Structure

> **Status**: Done (Milestone 1.2)
> **Source**: `runtime/shaders/runtime/include/` — pbr_common.hlsli, shadow_common.hlsli
> **Depends on**: Shader Compatibility (§07), Register Binding Convention (§13)

## Overview

The UzEngine shader system uses shared `.hlsli` include files to avoid code duplication across multiple render passes. Each include file is a self-contained module providing reusable functions and constants for common GPU tasks (PBR lighting, shadow sampling, etc.).

This document specifies:
- The directory structure and naming convention
- Include guard patterns and prerequisites
- How to add new shared functions
- Integration with both SM6.6 (bindless) and SM5 (register-bound) code paths

## Directory Structure

```
runtime/shaders/runtime/
├── include/
│   ├── pbr_common.hlsli         # PBR BRDF functions (GGX, Smith, Fresnel, Cook-Torrance)
│   ├── shadow_common.hlsli      # Cascaded shadow map sampling (PCF, cascade selection)
│   └── (future: ibl_common.hlsli, post_process_common.hlsli, etc.)
├── mesh_vs.hlsl                 # Vertex shader for all opaque meshes
├── mesh_ps.hlsl                 # Pixel shader with PBR + shadows
├── gbuffer_vs.hlsl              # G-Buffer geometry pass vertex shader
├── gbuffer_ps.hlsl              # G-Buffer write pass (no lighting)
├── deferred_lighting_ps.hlsl     # Deferred lighting pass (reads G-Buffer, evaluates PBR)
├── shadow_vs.hlsl               # Shadow depth pass vertex shader
├── shadow_ps.hlsl               # Shadow depth pass pixel shader (depth only)
├── fullscreen_vs.hlsl           # Simple fullscreen quad vertex shader
├── gbuffer_debug_ps.hlsl        # Debug visualizer for G-Buffer contents
└── (future: post_process_ps.hlsl, sky_ps.hlsl, etc.)
```

## Include Guard Convention

Every `.hlsli` file uses `#ifndef` guards to prevent multiple inclusion:

```hlsl
#ifndef PBR_COMMON_HLSLI
#define PBR_COMMON_HLSLI

// Function definitions and constants

#endif // PBR_COMMON_HLSLI
```

**Guard naming**: `<FILENAME_UPPERCASE>_HLSLI` (e.g., `SHADOW_COMMON_HLSLI` for `shadow_common.hlsli`).

## Include Syntax

Include files from runtime shaders using the `include/` subdirectory path:

```hlsl
#include "include/pbr_common.hlsli"
#include "include/shadow_common.hlsli"
```

**Path**: Relative to the shader file location (`runtime/shaders/runtime/`). Use forward slashes even on Windows.

## pbr_common.hlsli — PBR BRDF Functions

### Purpose

Provides Cook-Torrance BRDF evaluation, GGX normal distribution, Smith geometry function, and Schlick-Fresnel approximation. Used by all physically-based shaders (forward, deferred, etc.).

### Prerequisites (caller must declare)

- None. Functions are self-contained.
- Implicit: shader must have lights provided (e.g., from `PerFrameCB`).

### Exported Constants

```hlsl
static const float PI = 3.14159265359f;
static const float MIN_ROUGHNESS = 0.04f;  // Prevents delta-function singularity
```

### Exported Functions

| Function | Purpose |
|----------|---------|
| `float3 ACESFilm(float3 x)` | Filmic tonemapping (ACES Narkowicz 2015) |
| `float DistributionGGX(float NdotH, float alpha)` | GGX/Trowbridge-Reitz NDF |
| `float GeometrySchlickGGX(float NdotV, float alpha)` | Single-direction Schlick-GGX geometry |
| `float GeometrySmith(float NdotV, float NdotL, float alpha)` | Combined Smith G (view + light) |
| `float3 FresnelSchlick(float cosTheta, float3 F0)` | Schlick Fresnel approximation |
| `float3 EvaluateBRDF(...)` | Full Cook-Torrance evaluation (see signature below) |

### EvaluateBRDF Signature

```hlsl
/// Evaluates Cook-Torrance BRDF for a single light source.
///
/// Computes diffuse (Lambertian) + specular (GGX) with energy conservation.
/// Parameters:
/// - N:         world-space surface normal (normalized)
/// - V:         world-space view direction (normalized, toward camera)
/// - L:         world-space light direction (normalized, toward light)
/// - radiance:  incoming light radiance (color * intensity * attenuation)
/// - albedo:    surface base color (linear)
/// - metallic:  metallic factor [0,1]
/// - alpha:     roughness^2 (PBR alpha parameter)
/// - F0:        reflectance at normal incidence
///
/// Returns: outgoing radiance contribution from this light.
float3 EvaluateBRDF(float3 N, float3 V, float3 L, float3 radiance,
                    float3 albedo, float metallic, float alpha, float3 F0);
```

### Example Usage

```hlsl
#include "include/pbr_common.hlsli"

float3 ComputeLighting(float3 normalWS, float3 viewDirWS, float3 albedo,
                       float metallic, float roughness) {
    float alpha = roughness * roughness;
    float3 F0 = metallic ? albedo : float3(0.04, 0.04, 0.04);

    // Light direction from PerFrameCB
    float3 lightDir = normalize(lightDirection);
    float3 lightRadiance = lightColor * lightIntensity;

    float3 outColor = EvaluateBRDF(normalWS, viewDirWS, lightDir,
                                   lightRadiance, albedo, metallic, alpha, F0);
    return ACESFilm(outColor);  // Tonemap to LDR
}
```

## shadow_common.hlsli — Cascaded Shadow Sampling

### Purpose

Provides cascaded shadow map sampling with 3×3 percentage-closer filtering (PCF), cascade selection based on view depth, inter-cascade blending, and atlas boundary clamping. Supports both SM6.6 bindless and SM5 register-bound texture access.

### Prerequisites (caller must declare)

The including shader **MUST** declare these:

```hlsl
// Constant buffer at b4
cbuffer ShadowDataCB : register(b4) {
    row_major float4x4 cascadeViewProj[4];
    float4   cascadeSplits;
    uint     shadowAtlasIndex;     // SRV heap index (SM6.6 only)
    uint     cascadeCount;
    float    normalBias;
    float    depthBias;
    float    shadowMapSize;
};

// Comparison sampler at s1
SamplerComparisonState shadowSampler : register(s1);

// Shadow texture (SM5 only)
#ifndef UZ_BINDLESS
Texture2D<float> shadowAtlasTexture : register(t5);
#endif
```

### Exported Constants

```hlsl
static const float CASCADE_BLEND_BAND = 0.1f;  // Blend in last 10% of each cascade
static const float2 kCascadeOffsets[4] = {
    float2(0.0f, 0.0f),   // Cascade 0 — top-left quadrant
    float2(0.5f, 0.0f),   // Cascade 1 — top-right
    float2(0.0f, 0.5f),   // Cascade 2 — bottom-left
    float2(0.5f, 0.5f)    // Cascade 3 — bottom-right
};
```

### Exported Functions

| Function | Purpose |
|----------|---------|
| `float SampleShadowPCF(float2 atlasUV, float depth, float texelSize, float2 cascadeOffset)` | 3×3 PCF on atlas quadrant |
| `float SampleShadowMap(float3 positionWS, float3 normalWS, float viewDepth)` | Cascade selection → PCF → blending |

### SampleShadowMap Signature

```hlsl
/// Computes shadow factor using cascaded shadow maps.
///
/// Selects the tightest cascade containing the fragment, projects into light
/// space, applies normal/depth bias, and samples with 3×3 PCF. In the last
/// CASCADE_BLEND_BAND fraction of each cascade's range, blends with the next
/// cascade to eliminate seams.
///
/// Parameters:
/// - positionWS: fragment world-space position
/// - normalWS:   fragment world-space surface normal (normalized)
/// - viewDepth:  distance from camera to fragment along view axis
///
/// Returns: Shadow factor [0, 1]: 1.0 = fully lit, 0.0 = fully shadowed.
float SampleShadowMap(float3 positionWS, float3 normalWS, float viewDepth);
```

### Dual-Path Binding (SM6.6 vs SM5)

The include file automatically adapts to the shader model via `#ifdef UZ_BINDLESS`:

```hlsl
/// Inside SampleShadowPCF():
#ifdef UZ_BINDLESS
    Texture2D<float> shadowAtlas = ResourceDescriptorHeap[shadowAtlasIndex];
#else
    // SM5 path uses register-bound texture
    #define shadowAtlas shadowAtlasTexture
#endif
```

**No shader changes required** — the include handles both paths internally.

### Example Usage

```hlsl
#include "include/shadow_common.hlsli"

cbuffer ShadowDataCB : register(b4) {
    // ... (must declare ShadowDataCB)
};

SamplerComparisonState shadowSampler : register(s1);

#ifndef UZ_BINDLESS
Texture2D<float> shadowAtlasTexture : register(t5);
#endif

float4 main(PSInput input) : SV_Target {
    float3 normalWS = normalize(input.normalWS);
    float3 positionWS = input.positionWS;
    float viewDepth = length(positionWS - cameraPos.xyz);

    // Compute shadow factor with cascades, PCF, and blending
    float shadow = SampleShadowMap(positionWS, normalWS, viewDepth);

    float3 color = albedo * (1.0 - shadow * 0.5);  // Darken shadowed areas
    return float4(color, 1.0);
}
```

## Adding New Include Files

When creating a new shared include file:

1. **Name**: `<topic>_common.hlsli` (e.g., `ibl_common.hlsli`, `post_process_common.hlsli`)

2. **Structure**:
   ```hlsl
   /// Detailed description of what this include provides.
   /// List which shaders use it, and what prerequisites they must declare.

   #ifndef TOPIC_COMMON_HLSLI
   #define TOPIC_COMMON_HLSLI

   // ─────────────────────────────────────────────────────────────────────────────
   // Constants
   // ─────────────────────────────────────────────────────────────────────────────

   /// Brief description of constant.
   static const float kSomeValue = 1.23f;

   // ─────────────────────────────────────────────────────────────────────────────
   // Functions
   // ─────────────────────────────────────────────────────────────────────────────

   /// Full documentation with purpose, parameters, returns.
   float3 SomeFunction(float3 param1, float param2) {
       // Implementation
   }

   #endif // TOPIC_COMMON_HLSLI
   ```

3. **Documentation**: Every exported function and constant must have a `///` comment explaining:
   - Purpose (what does it compute?)
   - Parameters (what does each mean? what are valid ranges?)
   - Returns (what is the output range? success/failure conditions?)
   - Side effects (modifies state? texture lookups?)

4. **No external dependencies**: Include files should be self-contained. If you need another include, document it clearly in the file header.

## Rules

1. **ALWAYS use `#ifndef` guards** — prevents multiple-inclusion errors and accidental redefinition.

2. **ALWAYS document prerequisites** — if a function requires a certain constant buffer or texture, declare it at the top of the include file.

3. **ALWAYS use `register()` decorators in callers** — the include file doesn't bind registers; the calling shader must declare `register(b4)`, `register(s1)`, etc.

4. **NEVER assume SM6.6 or SM5** — always provide both paths via `#ifdef UZ_BINDLESS` in the include file. Callers automatically get the right path.

5. **ALWAYS match the C++ struct layout** — if `ShadowDataCB` is declared in C++ as:
   ```cpp
   struct ShadowDataCB {
       DirectX::XMFLOAT4X4 cascadeViewProj[4];
       DirectX::XMFLOAT4 cascadeSplits;
       uint32_t shadowAtlasIndex;
       // ...
   };
   ```
   Then the HLSL must match byte-for-byte (use `row_major` for matrices, etc.).

6. **NEVER duplicate code between includes** — if two functions are almost identical, extract the common part into a helper function.

7. **ALWAYS test both SM6.6 and SM5 paths** — even if you only have an SM6.6 GPU, the SM5 path must compile without errors.

## Existing Includes Reference

### pbr_common.hlsli

- **Lines**: 167
- **Functions**: 6 (ACES, GGX NDF, Schlick-GGX, Smith G, Fresnel, Cook-Torrance eval)
- **Used by**: mesh_ps.hlsl, deferred_lighting_ps.hlsl, (future: forward+ tiled lighting)
- **Dependencies**: None
- **Key reference**: Karis 2013, "Real Shading in Unreal Engine 4"

### shadow_common.hlsli

- **Lines**: 178
- **Functions**: 2 (PCF sampling, cascade selection + blending)
- **Used by**: mesh_ps.hlsl, deferred_lighting_ps.hlsl, (future: forward+ shadow pass)
- **Dependencies**: `ShadowDataCB` at `b4`, `shadowSampler` at `s1`, (SM5 only) `shadowAtlasTexture` at `t5`
- **Key reference**: Game Engine Architecture (Gregory, 3rd Ed.), cascaded shadow maps chapter

## Related Documentation

- [Register Binding Convention (§13-register-binding-convention.md)](13-register-binding-convention.md) — how constants and textures are bound
- [Shader Compatibility (§07-shader-compatibility.md)](07-shader-compatibility.md) — SM6.6 bindless vs SM5 dual-path design
- [Forward PBR Pass (§08-forward-pbr-pass.md)](08-forward-pbr-pass.md) — practical rendering example using both includes
- [Cook-Torrance BRDF (§09-cook-torrance-brdf.md)](09-cook-torrance-brdf.md) — detailed BRDF math and derivations
- [Shadow System (§06-shadow-system.md)](06-shadow-system.md) — cascaded shadow map architecture and parameters
