<!-- doc-meta
title: Data Flow — SkyLight, Fog Sky Injection, and Ambient
status: active
created: 2026-03-19
relates-to:
  - ue5-exponential-height-fog-defaults.md
  - ue5-environment-component-defaults.md
-->

# Data Flow: SkyLight + Fog Sky Injection + Ambient

## Problem Statement

UzEngine outdoor scenes have:
1. **Black shadows** — ambient fill in shadowed areas is too weak or zero.
2. **No horizon fog color transition** — UE5's Basic scene has blue/white horizon haze from sky atmosphere being injected into fog inscattering; UzEngine fog is grey.
3. **Ambient fallback too dark** — hardcoded `{0.08, 0.09, 0.12}` when IBL is unavailable.

This document traces the COMPLETE data flow in UE5 for these three systems and maps each stage to UzEngine's current implementation.

---

## Part 1: UE5 Reference — SkyLight Ambient Pipeline

### Stage 1: Sky Cubemap Capture

| Step | UE5 File:Line | Description |
|------|--------------|-------------|
| 1a | `SkyLightComponent.cpp:265` | `FSkyLightSceneProxy` stores `LightColor = FLinearColor(Component->LightColor) * Component->Intensity` |
| 1b | `ReflectionEnvironmentRealTimeCapture.cpp` | Renders sky into a cubemap (real-time or one-shot) |
| 1c | `ReflectionEnvironmentDiffuseIrradiance.cpp:88-231` | `ComputeDiffuseIrradiance()` — GPU pixel shader projects cubemap to 9 SH bands via mip-chain downsampling, then reads back 9x1 RGBA16F texture |

### Stage 2: SH Coefficient Packing (CPU)

| Step | UE5 File:Line | Description |
|------|--------------|-------------|
| 2a | `ReflectionEnvironment.cpp:623-669` | `SetupSkyIrradianceEnvironmentMapConstantsFromSkyIrradiance()` — Packs 9 raw SH coefficients into 7 float4s with diffuse convolution baked in. Uses `SqrtPI`, `Coefficient0..4` from "Stupid SH Tricks" |
| 2b | `ReflectionEnvironment.cpp:703-712` | Uploads 7 float4s + 1 float4 (AverageBrightness at index 7) to a StructuredBuffer `SkyIrradianceEnvironmentMap` |

### Stage 3: SkyLightColor Multiplier

| Step | UE5 File:Line | Description |
|------|--------------|-------------|
| 3a | `SkyLightComponent.cpp:222-225` | `GetEffectiveLightColor() = LightColor * GSkylightIntensityMultiplier * SpecifiedCubemapColorScale` |
| 3b | `SceneRendering.cpp:1886` | `ViewUniformShaderParameters.SkyLightColor = SkyLight->GetEffectiveLightColor()` (or `Black` if no skylight) |

### Stage 4: Shader Evaluation

| Step | UE5 File:Line | Description |
|------|--------------|-------------|
| 4a | `ReflectionEnvironmentShared.ush:84-100` | `GetSkySHDiffuse(Normal)` — evaluates L2 SH via 7 dot products against `SkyIrradianceEnvironmentMap[0..6]` |
| 4b | `SkyLightingDiffuseShared.ush:130` | `DiffuseLookup = GetSkySHDiffuse(Normal) * View.SkyLightColor.rgb` |
| 4c | `SkyLightingDiffuseShared.ush:133` | `Lighting = (SkyDiffuseLookUpMul * DiffuseLookup + OcclusionTint) * DiffuseColor * DiffuseWeight * PreExposure` |

### Key Point: SkyLightColor is the MULTIPLIER

UE5's SH coefficients in `SkyIrradianceEnvironmentMap` represent the raw sky irradiance. The `View.SkyLightColor` (= `Intensity * LightColor * CubemapColorScale`) is applied as a **separate multiplier** in the shader at evaluation time (line 130). This means:
- SH stores the raw captured sky data.
- `SkyLightColor` controls intensity/tint at render time without re-projecting SH.

---

## Part 2: UE5 Reference — Sky-to-Fog Color Injection

### The "DistantSkyLightLut" System

UE5 does NOT use SH L0 for fog color injection. It uses a dedicated 1-pixel buffer computed by a separate compute shader.

| Step | UE5 File:Line | Description |
|------|--------------|-------------|
| F1 | `SkyAtmosphereRendering.cpp:850-885` | `FRenderDistantSkyLightLutCS` — compute shader class, dispatches 8x8=64 threads |
| F2 | `SkyAtmosphere.usf:1359-1450` | `RenderDistantSkyLightLutCS` — each thread traces a uniform sphere direction through the atmosphere (10 samples), accumulates luminance in groupshared memory, parallel reduction sums to 1 value |
| F3 | `SkyAtmosphere.usf:1438-1444` | `Illuminance = AccumulatedSamples * (4*PI / 64)` then `Output = Illuminance * UniformPhaseFunction` where `UniformPhaseFunction = 1/(4*PI)`. Net result: average sky luminance over the full sphere |
| F4 | `SkyAtmosphereRendering.cpp:503-509` | Result stored in `DistantSkyLightLutBuffer` (StructuredBuffer, 1 float4) |
| F5 | `SceneRendering.cpp:1398-1399` | `DistantSkyLightLutBufferSRV` assigned to `View.DistantSkyLightLutBufferSRV` |
| F6 | `SkyAtmosphereCommon.ush:343-352` | `GetViewDistanceSkyLightColor()` reads `View.DistantSkyLightLutBufferSRV[0].rgb` |

### Fog Inscattering Color Injection

| Step | UE5 File:Line | Description |
|------|--------------|-------------|
| G1 | `FogRendering.cpp:267` | `View.SkyAtmosphereAmbientContributionColorScale = FogInfo.SkyAtmosphereAmbientContributionColorScale` (from ExponentialHeightFogComponent, default `(1,1,1,1)`) |
| G2 | `SceneRendering.cpp:1440` | `ViewUniformShaderParameters.SkyAtmosphereHeightFogContribution = SkyAtmosphereSceneProxy.GetHeightFogContribution()` (from SkyAtmosphereComponent, default `1.0`) |
| G3 | `SkyAtmosphereComponent.cpp:130` | `HeightFogContribution = 1.0f` (default value) |
| G4 | `HeightFogCommon.ush:174-180` | **THE INJECTION FORMULA:** |

```hlsl
// HeightFogCommon.ush:174-180
#if PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG
    float3 SkyAmbient = FogData.SkyAtmosphereAmbientContributionColorScale.rgb
                      * View.SkyAtmosphereHeightFogContribution.xxx
                      * GetViewDistanceSkyLightColor();
    Inscattering += SkyAmbient;
#endif
```

### What "GetViewDistanceSkyLightColor()" Actually Returns

This is **NOT** `View.SkyLightColor`. It is the average sky luminance over the full sphere, computed by `RenderDistantSkyLightLutCS` from the physical atmosphere model. It represents the scattered sky light illuminance assuming uniform phase function — essentially "how bright is the sky hemisphere right now."

For an Earth-like daytime atmosphere, this returns something like `(0.15, 0.25, 0.55)` — a blue-tinted value representing the scattered sky light.

---

## Part 3: UE5 Reference — Ambient Fallback

| Step | UE5 File:Line | Description |
|------|--------------|-------------|
| A1 | `SceneRendering.cpp:1899` | When no SkyLight exists: `SkyLightColor = FLinearColor::Black` |
| A2 | `ReflectionEnvironment.cpp:694-701` | When no SkyLight and buffer newly allocated: memset to zero |
| A3 | `SceneRendering.cpp:1942` | Fallback SRV = `GIdentityPrimitiveBuffer.SkyIrradianceEnvironmentMapSRV` (identity = zero) |

**UE5 has NO hardcoded ambient minimum.** When there is no SkyLight, ambient is literally zero — the scene relies entirely on direct lighting. The "ambient" comes from:
1. SkyLight SH irradiance (primary source)
2. Lumen GI / LPV (dynamic GI systems)
3. Baked lightmaps (static lighting)

Without any of these, shadowed areas ARE black in UE5. The reason UE5's Basic template looks good is that it **always has a SkyLight component** that captures the atmosphere cubemap.

---

## Part 4: UzEngine Current State

### SkyLight Ambient Pipeline

| UE5 Stage | UzEngine Status | File | Issue |
|-----------|----------------|------|-------|
| Sky cubemap capture | WORKING | `atmosphere_renderer.cpp:350-396` | 64x64 cubemap rendered per-frame via compute sky atmosphere shader |
| SH projection | WORKING (GPU readback) | `atmosphere_renderer.cpp:401-421`, `sh_projection.cpp` | `ProjectCubemapToSH()` reads back cubemap, projects on CPU. **BUT** returns zeros when readback fails |
| SH packing | WORKING | `sh_projection.cpp` | Uses same `SetupSkyIrradianceEnvironmentMapConstantsFromSkyIrradiance` formula as UE5 |
| SH upload to CB | WORKING | `frame_setup.cpp:241-248` | Uploads `skyIrradianceSH[7]` scaled by `indirectLightingIntensity` |
| SkyLightColor multiplier | **PARTIAL** | `frame_setup.cpp:226-229` | Uploads `skyLightColorR/G/B` and `skyLightIntensity` from `SkyLightComponent` |
| Shader SH evaluation | **DIFFERENT from UE5** | `deferred_lighting_ps.hlsl:965` | `GetSkySHDiffuse(N, skyIrradianceSH)` — returns raw SH value. Then multiplied by `skyLightIntensity * skyLightTint` on line 1031. **Problem: UE5 multiplies SH by SkyLightColor in shader (line 130 of SkyLightingDiffuseShared.ush), but we ALSO pre-scale SH by indirectLightingIntensity on CPU (frame_setup.cpp:240). This double-applies indirect intensity.** |
| Ambient fallback | **WRONG** | `light_renderer.h:64` | `ambientColor = {0.08, 0.09, 0.12}` — hardcoded. UE5 uses `{0, 0, 0}` when no SkyLight. |

### SH Readback Issue

The `ProjectCubemapToSH()` GPU readback path (`atmosphere_renderer.cpp:404`) copies the sky cubemap from GPU to CPU, then projects SH on the CPU. When this fails (returns zeros), the code falls back to zero SH (`atmosphere_renderer.cpp:418`), which means:
- **Black ambient** — no sky diffuse irradiance at all.
- The constant ambient fallback `{0.08, 0.09, 0.12}` becomes the only fill light.

### Sky-to-Fog Color Injection

| UE5 Stage | UzEngine Status | File | Issue |
|-----------|----------------|------|-------|
| DistantSkyLightLut compute | **MISSING** | N/A | UE5 dispatches a dedicated CS to integrate sky luminance over the sphere. UzEngine does not have this. |
| Fog inscattering injection | **PARTIALLY IMPLEMENTED** | `fog_renderer.cpp:200-204`, `height_fog_ps.hlsl:247-249` | Uses SH L0 as approximation of `GetViewDistanceSkyLightColor()`. `skyLightColor` in HeightFogCB is set to `m_skyIrradianceSH.coeffs[0..2].w` (the L0 terms, which are the average sky irradiance per channel). |
| SkyAtmosphereAmbientContributionColorScale | WORKING | `fog_renderer.cpp:200` | Defaults to `{1,1,1}` (UE5 parity) |
| HeightFogContribution | WORKING | `fog_renderer.cpp:211` | Read from `SkyAtmosphereComponent.heightFogContribution` |
| FogInscatteringLuminance default | **CORRECT** | `height_fog_component.h:37` | Default `{0,0,0}` (UE5 post-SkyAtmosphere default) |

**The issue**: When SH readback returns zeros, `skyLightColor` in the fog CB is also zero. This means the sky injection formula `skyAtmContribColorScale * heightFogContribution * skyLightColor` produces zero — no blue horizon haze.

### Ambient Fallback

| UE5 Stage | UzEngine Status | File | Issue |
|-----------|----------------|------|-------|
| No-SkyLight ambient | **WRONG** | `light_renderer.h:64` | Hardcoded `{0.08, 0.09, 0.12}`. UE5 uses `{0, 0, 0}`. However, removing this without a working SkyLight SH would make shadows pitch black. |
| SkyLightMinAmbient | WORKING | `constant_buffers.h:68` | `skyLightMinAmbient = 0.0f` (UE5 default). Used in shader as floor. |

---

## Part 5: Data Flow Diagram

```
                          UE5 PIPELINE
                          ============

  SkyAtmosphereComponent
         |
         v
  [Sky Cubemap Capture]  ──────────────────────┐
         |                                      |
         v                                      v
  [ComputeDiffuseIrradiance]            [RenderDistantSkyLightLutCS]
  (GPU: mip-chain SH project)          (GPU: 64 sphere samples, parallel sum)
         |                                      |
         v                                      v
  SkyIrradianceEnvironmentMap[0..7]     DistantSkyLightLutBufferSRV[0].rgb
  (StructuredBuffer, 8 float4)          (StructuredBuffer, 1 float4)
         |                                      |
         v                                      |
  SkyLightColor = Intensity * Color     View.SkyAtmosphereHeightFogContribution
         |                              View.SkyAtmContribColorScale
         v                                      |
  [Deferred Lighting Shader]                    v
  irradiance = GetSkySHDiffuse(N)       [Height Fog Shader]
             * View.SkyLightColor       Inscatter += ContribScale
                                                   * HeightFogContrib
                                                   * GetViewDistanceSkyLightColor()
```

```
                        UzEngine PIPELINE (CURRENT)
                        ===========================

  SkyAtmosphereComponent
         |
         v
  [Sky Cubemap Render (64x64)]  ────────────────┐
         |                                       |
         v                                       v
  [ProjectCubemapToSH — CPU readback]    SH L0 = coeffs[0..2].w
  (GPU→CPU copy, then CPU projection)    (APPROXIMATION of DistantSkyLightLut)
         |                                       |
    [FAILS → zeros]                              |
         |                                       v
         v                               HeightFogCB.skyLightColor = SH L0
  PerFrameCB.skyIrradianceSH[7]                  |
  (scaled by indirectLightingIntensity)          v
         |                               [Height Fog Shader]
         v                               inscatter += skyAtmContribColorScale
  [Deferred Lighting Shader]                        * heightFogContribution
  irradiance = GetSkySHDiffuse(N, SH)              * skyLightColor
             * skyLightIntensity                    ~~~~ (ZERO when readback fails)
             * skyLightTint

  FALLBACK: ambientColor = {0.08, 0.09, 0.12}
            ^^^^^ WRONG — UE5 uses {0, 0, 0}
```

---

## Part 6: Root Causes and Required Changes

### Root Cause 1: SH Readback May Return Zeros

The GPU readback path (`ProjectCubemapToSH` with `CopyTextureToBuffer`) is fragile. When it fails, both the deferred lighting ambient AND the fog sky injection get zero, producing black shadows and grey fog.

**Evidence needed**: Check log output for `"SH readback returned zeros"` message.

### Root Cause 2: UzEngine Uses SH L0 Instead of DistantSkyLightLut

UE5 computes `GetViewDistanceSkyLightColor()` from a dedicated compute shader that integrates sky luminance over 64 uniform sphere directions. UzEngine approximates this with the SH L0 coefficient (the DC term of the SH projection).

**This approximation is actually reasonable** — SH L0 is the average irradiance, which is conceptually the same as what `RenderDistantSkyLightLutCS` computes (average sky luminance * uniform phase function). The magnitudes may differ slightly because:
- UE5: integrates actual sky radiance over sphere, applies uniform phase function
- UzEngine: SH L0 already has diffuse convolution baked in

However, this difference is secondary to the readback failure.

### Root Cause 3: Hardcoded Ambient Fallback

`LightFrameData.ambientColor = {0.08, 0.09, 0.12}` is an invented value. UE5 has zero ambient when no SkyLight exists. But removing it without fixing the SH readback would make things worse.

---

## Part 7: Changes Required (Dependency Order)

### Change 1: Fix SH Readback Reliability

**Files**: `atmosphere_renderer.cpp`, potentially `sh_projection.cpp`

**Problem**: `ProjectCubemapToSH(device, cmdList, cubemap, faceSize)` may fail silently, returning zero SH. The GPU copy from cubemap to readback buffer may not complete before the CPU read.

**Action**:
- Verify the GPU→CPU copy is fenced properly (WaitIdle must happen BEFORE Map/Read).
- Add diagnostic logging of raw readback data (first few pixels of each face).
- If readback is fundamentally unreliable, implement a GPU-only SH projection using a compute shader (like UE5's `ComputeDiffuseIrradiance`).

**This is the #1 priority.** Everything else depends on SH being non-zero.

### Change 2: Implement DistantSkyLightLut (or Validate SH L0 Approximation)

**Files**: `atmosphere_renderer.cpp` (new compute dispatch), `constant_buffers.h` (new field or reuse), `fog_renderer.cpp`, `height_fog_ps.hlsl`

**Option A (Accurate)**: Implement a 1-dispatch compute shader that integrates sky luminance over 64 uniform sphere samples, stores result in a 1-element buffer. This matches UE5 exactly.

**Option B (Pragmatic)**: Keep using SH L0 but ensure:
1. SH L0 values are non-zero (Fix Change 1).
2. The magnitude scaling matches UE5's `DistantSkyLightLut`. The SH L0 with diffuse convolution baked in has a factor of `1/(2*sqrt(PI))` that the DistantSkyLightLut does not. May need to scale by `2*sqrt(PI)` to compensate. **Must verify empirically.**

### Change 3: Fix SkyLightColor Multiplier Chain

**Files**: `frame_setup.cpp`, `deferred_lighting_ps.hlsl`

**Problem**: UE5 applies `SkyLightColor` (= `Intensity * LightColor`) as a **shader-time multiplier** on the raw SH result. UzEngine pre-scales SH by `indirectLightingIntensity` on the CPU AND multiplies by `skyLightIntensity * skyLightTint` in the shader. This means `indirectLightingIntensity` is applied twice if both paths are used.

**UE5 behavior**:
- SH coefficients are raw (no intensity scaling)
- `SkyLightColor = LightColor * Intensity` (set in `SceneRendering.cpp:1886`)
- Shader: `irradiance = GetSkySHDiffuse(N) * View.SkyLightColor.rgb`
- `IndirectLightingIntensity` is used separately for the diffuse term only

**Action**: Align with UE5:
- CPU: Upload raw SH coefficients (no `indirectLightingIntensity` scaling).
- Shader: `irradiance = GetSkySHDiffuse(N) * skyLightColor.rgb` where `skyLightColor = intensity * lightColor * indirectLightingIntensity`.
- Or equivalently: keep current approach but ensure `indirectLightingIntensity` is only applied once.

### Change 4: Remove Hardcoded Ambient Fallback

**Files**: `light_renderer.h`

**Action**: Change `ambientColor = {0.08, 0.09, 0.12}` to `{0.0, 0.0, 0.0}` to match UE5. The ambient contribution must come entirely from the SkyLight SH system.

**Prerequisite**: Changes 1 and 3 must be working first, otherwise shadows go completely black.

### Change 5: Ensure HeightFogComponent.color Defaults to Black

**Files**: `height_fog_component.h`

**Status**: DELIBERATELY NOT APPLIED (2026-10-23). The reference default is black because
the sky injection supplies the fog colour there. Our injection (`height_fog_ps.hlsl:135-137`)
scales the authored sky-light colour, and with `color = black` the `sample/demo` fog renders as
a dark, warm haze that hides the ground (compared against the authored colour on 2026-10-23).
The authored default `color = {0.45, 0.55, 0.65}` (with `directionalInscatteringColor =
{1.0, 0.85, 0.5}` and `density = 0.15`) stays until the injection reproduces the sky
luminance. Do not "correct" the default to black again without fixing the injection first;
the 2026-10-10 revert that did so was rolled back.

### Change 6: Verify SkyAtmosphereAmbientContributionColorScale Default

**Files**: `height_fog_component.h`, `fog_renderer.cpp`

**Status**: The HeightFogComponent does not have a `skyAtmosphereAmbientContributionColorScale` field. It is hardcoded to `{1,1,1}` in `fog_renderer.cpp:200`. This matches UE5's default.

**Potential future action**: Add this as a field on `HeightFogComponent` for artistic control (UE5 exposes it as a property).

---

## Part 8: Sub-Tasks

### Sub-Task 1: Diagnose SH Readback (1 file)
- **File**: `atmosphere_renderer.cpp`
- **Action**: Add detailed logging after `ProjectCubemapToSH()`: dump first 3 coefficients, check for all-zero, log face copy offsets. Run demo and check console output.

### Sub-Task 2: Fix SH Readback Fencing (1-2 files)
- **File**: `sh_projection.cpp`, `atmosphere_renderer.cpp`
- **Action**: Ensure `WaitIdle()` is called between `cmdList->Execute()` (that copies cubemap) and `Map()` (that reads the staging buffer). Verify the copy commands are flushed.

### Sub-Task 3: Validate SH L0 Magnitude for Fog Injection (2 files)
- **File**: `frame_lighting.cpp`, `fog_renderer.cpp`
- **Action**: Log `skyFogR/G/B` values after SH is computed. Compare against expected sky color for an Earth-like daytime atmosphere. Expected: ~`(0.05-0.3, 0.1-0.4, 0.2-0.6)` range for blue sky.

### Sub-Task 4: Fix Ambient Multiplier Chain (2 files)
- **File**: `frame_setup.cpp` (SH scaling), `deferred_lighting_ps.hlsl` (if shader changes needed)
- **Action**: Decide on one of:
  - (A) Remove `shScale` from CPU-side SH upload; apply `indirectLightingIntensity` only in shader.
  - (B) Keep CPU-side scaling but remove duplicate multiplication in shader.
  - Ensure `skyLightIntensity * skyLightTint` in shader matches UE5's `View.SkyLightColor`.

### Sub-Task 5: Remove Hardcoded Ambient (1 file)
- **File**: `light_renderer.h`
- **Action**: Change `ambientColor` default to `{0, 0, 0}`.

### Sub-Task 6: Add SkyAtmosphereAmbientContributionColorScale to HeightFogComponent (2 files)
- **File**: `height_fog_component.h`, `fog_renderer.cpp`
- **Action**: Add `Vec3 skyAtmosphereAmbientContributionColorScale = {1,1,1}` field. Read it in `fog_renderer.cpp` instead of hardcoding.

### Sub-Task 7 (Optional): Implement GPU-only SH Projection (3 files)
- **Files**: New compute shader, `atmosphere_renderer.cpp`, `sh_projection.h`
- **Action**: Port UE5's `ComputeDiffuseIrradiance` approach (mip-chain accumulation + readback of 9x1 texture) to avoid the full cubemap GPU→CPU copy. This is more robust than copying all 6 faces.

### Sub-Task 8 (Optional): Implement DistantSkyLightLut Compute Shader (3 files)
- **Files**: New compute shader, `atmosphere_renderer.cpp`, `fog_renderer.cpp`
- **Action**: Port `RenderDistantSkyLightLutCS` — 64 uniform sphere samples through atmosphere, parallel reduction to 1 float4. More accurate than SH L0 for fog injection.

---

## Part 9: Recommended Implementation Order

1. **Sub-Task 1** — Diagnose (is SH actually zero? or is it non-zero but too weak?)
2. **Sub-Task 2** — Fix readback fencing (if SH is zero due to readback failure)
3. **Sub-Task 3** — Validate magnitude (if SH is non-zero but fog still grey)
4. **Sub-Task 4** — Fix multiplier chain (ensure correct intensity application)
5. **Sub-Task 5** — Remove hardcoded ambient (after SH is working)
6. **Sub-Task 6** — Add component field (optional, for artistic control)
7. **Sub-Task 7** — GPU-only SH (robustness improvement)
8. **Sub-Task 8** — DistantSkyLightLut (accuracy improvement)

Sub-tasks 1-5 should fix the visible problem. Sub-tasks 6-8 are refinements for accuracy and robustness.

---

## Appendix A: UE5 SH Coefficient Packing Formula

From `ReflectionEnvironment.cpp:623-669`:

```cpp
// SetupSkyIrradianceEnvironmentMapConstantsFromSkyIrradiance
const float SqrtPI = FMath::Sqrt(PI);
const float Coefficient0 = 1.0f / (2 * SqrtPI);
const float Coefficient1 = FMath::Sqrt(3.f) / (3 * SqrtPI);
const float Coefficient2 = FMath::Sqrt(15.f) / (8 * SqrtPI);
const float Coefficient3 = FMath::Sqrt(5.f) / (16 * SqrtPI);
const float Coefficient4 = 0.5f * Coefficient2;

// coeffs[0..2]: L1 terms (xyz) + L0 term (w) per channel
// coeffs[0].W = Coefficient0 * R_L0 - Coefficient3 * R_L6
// coeffs[3..5]: L2 terms per channel
// coeffs[6]: Y22 cross-term per channel, w=1
```

From `ReflectionEnvironmentShared.ush:84-100`:

```hlsl
float3 GetSkySHDiffuse(float3 Normal) {
    float4 NormalVector = float4(Normal, 1.0f);
    float3 Intermediate0, Intermediate1, Intermediate2;
    Intermediate0.x = dot(SkyIrradianceEnvironmentMap[0], NormalVector);
    Intermediate0.y = dot(SkyIrradianceEnvironmentMap[1], NormalVector);
    Intermediate0.z = dot(SkyIrradianceEnvironmentMap[2], NormalVector);
    float4 vB = NormalVector.xyzz * NormalVector.yzzx;
    Intermediate1.x = dot(SkyIrradianceEnvironmentMap[3], vB);
    Intermediate1.y = dot(SkyIrradianceEnvironmentMap[4], vB);
    Intermediate1.z = dot(SkyIrradianceEnvironmentMap[5], vB);
    float vC = NormalVector.x * NormalVector.x - NormalVector.y * NormalVector.y;
    Intermediate2 = SkyIrradianceEnvironmentMap[6].xyz * vC;
    return max(0, Intermediate0 + Intermediate1 + Intermediate2);
}
```

## Appendix B: UE5 DistantSkyLightLut Formula

From `SkyAtmosphere.usf:1359-1450`:

```hlsl
// 64 threads, each traces one uniform sphere direction
float3 SampleDir = UniformSphereSamplesBuffer[LinearIndex].xyz;
SingleScatteringResult ss = IntegrateSingleScatteredLuminance(...);
GroupSkyLuminanceSamples[LinearIndex] = ss.L * SkyLuminanceFactor;

// Parallel reduction (sum 64 samples)
...

// Final output (thread 0 only)
float3 Illuminance = AccumulatedSamples * (4*PI / 64);
float3 UniformPhaseFunction = 1.0f / (4.0f * PI);
Output = float4(Illuminance * UniformPhaseFunction, 0);
// Simplifies to: Output = AccumulatedSamples / 64
```

## Appendix C: UE5 Fog Injection Shader Formula

From `HeightFogCommon.ush:140-183`:

```hlsl
float3 ComputeInscatteringColor(...) {
    half3 Inscattering = FogData.ExponentialFogColorParameter.xyz;
    // ... cubemap lookup (if enabled) ...

    #if PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG
    float3 SkyAmbient = FogData.SkyAtmosphereAmbientContributionColorScale.rgb
                      * View.SkyAtmosphereHeightFogContribution.xxx
                      * GetViewDistanceSkyLightColor();
    Inscattering += SkyAmbient;
    #endif

    return Inscattering;
}
```

The three multiplicands:
1. `SkyAtmosphereAmbientContributionColorScale` — artistic tint on the ExponentialHeightFogComponent (default `{1,1,1}`)
2. `SkyAtmosphereHeightFogContribution` — scalar on the SkyAtmosphereComponent (default `1.0`)
3. `GetViewDistanceSkyLightColor()` — average sky luminance from the DistantSkyLightLut buffer
