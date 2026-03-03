# IBL Pipeline Spec

> **Status**: Planned (Phase 2)
> **Priority**: P0 — Rendering Blocker
> **Rationale**: The PBR lighting shader references `SampleEnvironmentIrradiance()` and `SamplePrefilteredEnv()` functions with no actual data source. Metallic surfaces appear black in shadow, specular reflections are missing. Must be implemented before Phase 2 PBR renderer ships.

---

## Overview

**Image-Based Lighting (IBL)** provides ambient diffuse and specular lighting from HDR environment maps. Without IBL, PBR surfaces lose all ambient energy — metallic materials appear flat/black outside direct light cones.

Pipeline:
1. Import HDR `.exr`/`.hdr` equirectangular map → cubemap
2. **Diffuse**: Convolve cubemap → irradiance cubemap (9 SH or 128×128 cubemap)
3. **Specular**: Pre-filter cubemap at N mip levels (roughness 0→1) → prefiltered env map
4. **BRDF LUT**: Integrate split-sum BRDF → 512×512 LUT texture
5. Runtime: sample irradiance + prefiltered env + BRDF LUT in PBR lighting shader

---

## Asset Inputs

| Format | Description |
|--------|-------------|
| `.exr` | OpenEXR 32-bit HDR equirectangular |
| `.hdr` | Radiance HDR equirectangular |
| `.uzenv` | Engine-side compiled IBL asset (cubemap + irradiance + prefiltered + LUT) |

---

## Offline Baking Pipeline

### Step 1: Equirect → Cubemap

```hlsl
// Convert equirectangular lat-long to cubemap
// Dispatch: 6 * resolution * resolution / 8x8 thread groups
[numthreads(8, 8, 1)]
void CS_EquirectToCubemap(uint3 tid : SV_DispatchThreadID) {
    float3 dir = CubeFaceDirection(tid.z, tid.xy, g_Resolution);
    float2 uv  = DirToEquirectUV(dir);
    g_OutputCube[tid] = g_InputEquirect.SampleLevel(g_LinearSampler, uv, 0);
}
```

### Step 2: Diffuse Irradiance (Cosine-weighted convolution)

```hlsl
// Convolve hemisphere over each cubemap texel
// Output: 64×64 irradiance cubemap (sufficient for low-frequency diffuse)
[numthreads(8, 8, 1)]
void CS_IrradianceConvolution(uint3 tid : SV_DispatchThreadID) {
    float3 N     = CubeFaceDirection(tid.z, tid.xy, 64u);
    float3 irr   = float3(0, 0, 0);
    uint   count = 0;

    for (float phi = 0; phi < TWO_PI; phi += SAMPLE_DELTA) {
        for (float theta = 0; theta < HALF_PI; theta += SAMPLE_DELTA) {
            float3 L = SphericalToCartesian(phi, theta, N);
            irr  += g_EnvCube.SampleLevel(g_LinearSampler, L, 0).rgb * cos(theta) * sin(theta);
            count++;
        }
    }
    g_IrrCube[tid] = float4(PI * irr / float(count), 1.0f);
}
```

**Alternative (faster)**: Spherical Harmonics projection — 9 coefficients capture all low-frequency diffuse. Recommended for real-time (no cubemap sample at runtime, just dot product).

### Step 3: Specular Prefiltered Env Map (Split-Sum Approximation)

```hlsl
// Mip 0 = roughness 0 (mirror), Mip N = roughness 1 (fully diffuse)
// Uses Importance Sampling with GGX NDF
[numthreads(8, 8, 1)]
void CS_PrefilteredEnv(uint3 tid : SV_DispatchThreadID) {
    float roughness = g_MipLevel / float(g_MaxMip);
    float3 N = CubeFaceDirection(tid.z, tid.xy, g_Resolution >> g_MipLevel);
    float3 R = N; float3 V = N;  // Assume V = N (isotropic)

    float3 prefilteredColor = float3(0, 0, 0);
    float  totalWeight = 0;
    const uint SAMPLE_COUNT = 1024u;

    for (uint i = 0; i < SAMPLE_COUNT; i++) {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H  = ImportanceSampleGGX(Xi, N, roughness);
        float3 L  = normalize(2.0 * dot(V, H) * H - V);
        float  NdotL = saturate(dot(N, L));
        if (NdotL > 0) {
            prefilteredColor += g_EnvCube.SampleLevel(g_LinearSampler, L, 0).rgb * NdotL;
            totalWeight += NdotL;
        }
    }
    g_PrefilteredCube[tid] = float4(prefilteredColor / totalWeight, 1.0f);
}
```

### Step 4: BRDF Integration LUT

```hlsl
// Precompute split-sum BRDF integral: stores (scale, bias) for F0
// Output: 512×512 RG16F texture. X = scale (F0 independent), Y = bias
[numthreads(8, 8, 1)]
void CS_BRDFIntegration(uint3 tid : SV_DispatchThreadID) {
    float NdotV    = (tid.x + 0.5f) / 512.0f;
    float roughness = (tid.y + 0.5f) / 512.0f;
    float3 V = float3(sqrt(1.0 - NdotV*NdotV), 0, NdotV);
    float3 N = float3(0, 0, 1);

    float A = 0, B = 0;
    const uint SAMPLE_COUNT = 1024u;
    for (uint i = 0; i < SAMPLE_COUNT; i++) {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H  = ImportanceSampleGGX(Xi, N, roughness);
        float3 L  = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = saturate(L.z);
        float NdotH = saturate(H.z);
        float VdotH = saturate(dot(V, H));
        if (NdotL > 0) {
            float G  = GeometrySmith_IBL(N, V, L, roughness);
            float Gc = G * VdotH / (NdotH * NdotV);
            float Fc = pow(1.0 - VdotH, 5.0);
            A += (1.0 - Fc) * Gc;
            B += Fc * Gc;
        }
    }
    g_BRDFLut[tid.xy] = float2(A, B) / float(SAMPLE_COUNT);
}
```

---

## Runtime Shader Integration

```hlsl
// In pbr_lighting.hlsli — referenced by GBuffer resolve pass

TextureCube  g_IrradianceCube   : register(t10);
TextureCube  g_PrefilteredEnv   : register(t11);
Texture2D    g_BRDFLut          : register(t12);

float3 ComputeIBL(float3 N, float3 V, float3 albedo, float metallic, float roughness) {
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float  NdotV = saturate(dot(N, V));
    float3 R     = reflect(-V, N);

    // Diffuse IBL
    float3 irradiance = g_IrradianceCube.Sample(g_LinearSampler, N).rgb;
    float3 kS = FresnelSchlickRoughness(NdotV, F0, roughness);
    float3 kD = (1.0 - kS) * (1.0 - metallic);
    float3 diffuse = kD * albedo * irradiance;

    // Specular IBL (split-sum)
    float  maxMip = g_IBLParams.prefilteredMipLevels - 1.0;
    float3 prefilteredColor = g_PrefilteredEnv.SampleLevel(g_LinearSampler, R, roughness * maxMip).rgb;
    float2 brdf = g_BRDFLut.Sample(g_ClampSampler, float2(NdotV, roughness)).rg;
    float3 specular = prefilteredColor * (F0 * brdf.x + brdf.y);

    return (diffuse + specular) * g_IBLParams.intensity;
}
```

---

## .uzenv Asset Format

```
[uzenv]
version      = 1
source       = "environments/studio.exr"
resolution   = 512          ; Cubemap face resolution
irr_res      = 64           ; Irradiance cubemap (or "sh9" for SH)
prefilter_mips = 7          ; Roughness mip levels (0..6)
brdf_lut_res = 512
hdr_exposure = 1.0
rotation_y   = 0.0          ; Sky rotation in degrees
```

Compiled output stored in `.uzpak` — cubemap DDS (BC6H) + irradiance DDS + LUT DDS.

---

## IBL Params Constant Buffer

```cpp
struct IBLParams {
    float intensity          = 1.0f;  // Global IBL multiplier
    float prefilteredMipLevels = 7.0f;
    float horizonOcclusionStrength = 0.3f;  // Darken underside
    float _pad;
};
```

Bound at `b2` in `GlobalParams` cbuffer alongside sun/shadow params.

---

## Scene Integration

```toml
# .uzscene
[environment]
ibl          = "environments/studio.uzenv"
ibl_intensity = 1.0
sky_material  = "materials/sky_hdri.uzmat"
```

IBL asset loaded once per scene, bound globally. Multiple IBL zones (blending) is Phase 4.

---

## IPC Handlers

| Handler | Input | Output |
|---------|-------|--------|
| `scene.set_ibl` | `{ path, intensity }` | `{ ok }` |
| `scene.get_ibl_info` | `{}` | `{ path, intensity, mip_levels }` |

---

## Implementation Plan

| Week | Deliverable |
|------|-------------|
| 1 | Offline bakers: equirect→cubemap, irradiance conv, prefiltered env, BRDF LUT |
| 2 | `.uzenv` asset format + importer + DDS BC6H output |
| 3 | Runtime binding in PBR lighting shader, `ComputeIBL()` function |
| 4 | IPC handlers, scene config, Editor environment panel |

---

## Dependencies

- Phase 2 PBR renderer (`pbr_lighting.hlsl`)
- RHI TextureCube support
- Asset pipeline (DDS BC6H compression)
- `.uzproj`/`.uzscene` config parser

## Related Specs

- [Renderer](renderer.md)
- [Shader Library](shader-library.md)
- [Asset Pipeline](../assets/asset-pipeline.md)
- [Screen Space Effects](screen-space-effects.md)
