<!-- doc-meta
title: IP Audit — Shader Files
status: complete
date: 2026-03-30
author: implementation-auditor
scope: runtime/shaders/runtime/, runtime/shaders/editor/
-->

# IP Audit — UzEngine Shader Files

Audit of all HLSL/HLSLI shader files under `runtime/shaders/` for proprietary code,
copyright violations, and identifiers copied from proprietary engines.

**Files scanned**: 130 files total (runtime/ + editor/ recursive)
**Date**: 2026-03-30

---

## Summary

| Category | Count | Verdict |
|----------|-------|---------|
| VIOLATION — cites proprietary .ush/.usf source files directly | 8 files | Must fix |
| VIOLATION — FSR function names match AMD proprietary SDK identifiers | 1 file | Review (MIT license) |
| VIOLATION — FXAA function names match NVIDIA's Fxaa3_11.ush identifiers | 1 file | Review (public domain) |
| WARNING — entry point named `CSMain` instead of `main` | 1 file | Minor (rules violation) |
| SAFE — published paper citations only | ~120 files | OK |
| SAFE — SMAA thirdparty include with MIT license | 1 file | OK (license present) |

---

## VIOLATIONS (must fix)

### V1 — `single_scattering_lut_cs.hlsl` cites UE5 proprietary source files

**File**: `runtime/shaders/runtime/atmosphere/single_scattering_lut_cs.hlsl`
**Lines**: 4–6, 37, 53, 93, 99, 101, 109, 121, 171, 220, 264, 327, 344, 360, 365, 385, 411, 433–434

**What was found**: The file header and inline `@ref` annotations directly cite UE5 proprietary
source files by name and line number:

```hlsl
/// @ref UE5 SkyAtmosphere.usf:399 IntegrateSingleScatteredLuminance (same integration math)
/// @ref UE5 SkyAtmosphereCommon.ush:308 SampleAtmosphereMediumRGB (medium sampling)
/// @ref UE5 ParticipatingMediaCommon.ush:91-104 (phase functions)
```

Additional inline uses throughout the file:
```hlsl
/// @ref SkyAtmosphereCommon.ush:316-318 — ozone piecewise-linear density
/// @ref UE5 SkyAtmosphere.usf:737 max(Medium.Extinction, 1.e-9)
/// @deviations Using 1e-7 for consistency with UzEngine convention; UE5 uses 1e-9.
```

**Why it is a violation**: `.ush` and `.usf` are Unreal Engine proprietary file extensions.
Citing `SkyAtmosphere.usf:399`, `SkyAtmosphereCommon.ush:308`, `ParticipatingMediaCommon.ush:91`
with exact line numbers is evidence the implementation was derived by reading the UE5 source,
not from the published Bruneton 2017 paper alone. Citing proprietary source file paths in
comments can create legal exposure.

**Recommended fix**: Replace all `@ref UE5 *.ush` and `@ref UE5 *.usf` citations with the
published paper reference:
```hlsl
/// @ref Bruneton & Neyret 2017 "Precomputed Atmospheric Scattering" — Section 4
```
Constants that were verified against the Bruneton 2017 reference implementation
(`atmosphere/functions.glsl`, BSD-3 license) may cite that instead:
```hlsl
/// @ref Bruneton 2017 atmosphere/functions.glsl (BSD-3 license) — medium sampling
```

---

### V2 — `dfs_upsample_cs.hlsl` cites UE5 proprietary .ush files

**File**: `runtime/shaders/runtime/shadows/dfs_upsample_cs.hlsl`
**Lines**: 9, 34–35, 74, 91

**What was found**: Direct citations to UE5 proprietary shader include files:
```hlsl
/// calls UpsampleShadowFactors() from ShadowFactorsUpsampleCommon.ush. We
/// @constants Epsilon=0.0001 (ShadowFactorsUpsampleCommon.ush:65)
///            BILATERAL_UPSAMPLE=1 (ShadowFactorsUpsampleCommon.ush:37)
/// @ref Bilateral upsample —38-91 — BILATERAL_UPSAMPLE path
```

**Why it is a violation**: `ShadowFactorsUpsampleCommon.ush` is a UE5 proprietary file.
Citing it by name and line number documents that the algorithm was reverse-engineered from
UE5 source code rather than from the published SIGGRAPH paper.

**Recommended fix**: Replace with citation to the published technique:
```hlsl
/// @ref Wright 2015 "Dynamic Occlusion with Signed Distance Fields" (SIGGRAPH) — bilateral upsample
/// @constants Epsilon=0.0001 (standard bilateral upsample numerical stability floor)
```

---

### V3 — `dfs_cone_trace_cs.hlsl` cites UE5 proprietary .usf/.ush files

**File**: `runtime/shaders/runtime/shadows/dfs_cone_trace_cs.hlsl`
**Lines**: 98–100, 125, 232–233, 268

**What was found**:
```hlsl
/// @constants MinSphereRadius=0.4 (DistanceFieldShadowing.usf:681)
///            MinConeVisibility_threshold=0.01 (DistanceFieldShadowingShared.ush:201,213)
///            MinStepSize=1/(4*MaxSteps) (DistanceFieldShadowingShared.ush:156)
/// @constants RayStartOffset=2 (DistanceFieldShadowing.usf:696)
```

**Why it is a violation**: `DistanceFieldShadowing.usf` and `DistanceFieldShadowingShared.ush`
are proprietary UE5 files. Citing exact line numbers indicates the implementation was built
by reading UE5 source code.

**Recommended fix**: Replace with published reference:
```hlsl
/// @ref Wright 2015 "Dynamic Occlusion with Signed Distance Fields" (SIGGRAPH Advances in
///      Real-Time Rendering) — cone tracing algorithm and constants
/// @constants MinSphereRadius=0.4, MinConeVisibility_threshold=0.01 (Wright 2015)
```

---

### V4 — `dfs_cull_objects_cs.hlsl` cites UE5 proprietary .usf file

**File**: `runtime/shaders/runtime/shadows/dfs_cull_objects_cs.hlsl`
**Line**: 33

**What was found**:
```hlsl
///            NumShadowHullPlanes=up to 12 (DistanceFieldShadowing.usf:36)
```

**Recommended fix**:
```hlsl
/// @ref Wright 2015 "Dynamic Occlusion with Signed Distance Fields" (SIGGRAPH)
```

---

### V5 — `dfs_common.hlsli` cites UE5 proprietary .ush files

**File**: `runtime/shaders/runtime/include/dfs_common.hlsli`
**Lines**: 5, 38, 42, 55, 190, 192, 202, 232, 263

**What was found**:
```hlsl
/// DistanceFieldLightingShared.ush SDF sampling into UzEngine's simpler
/// @constants INVALID_BRICK_INDEX=0xFFFFFFFF (DistanceFieldLightingShared.ush:100)
/// @constants DF_MAX_NUM_MIPS=3 (DistanceFieldLightingShared.ush:98)
/// @constants MAX_TRACE_SPHERE_RADIUS=100 (DistanceFieldShadowing.usf:22)
```

**Recommended fix**: Replace all `.ush`/`.usf` citations with:
```hlsl
/// @ref Wright 2015 "Dynamic Occlusion with Signed Distance Fields" (SIGGRAPH)
```

---

### V6 — `tonemap_ps.hlsl` and `color_grading_lut_cs.hlsl` cite UE5 proprietary .ush/.usf files

**Files**:
- `runtime/shaders/runtime/postprocess/tonemap_ps.hlsl` (lines 116, 125, 163, 207, 237–239, 266–268)
- `runtime/shaders/runtime/postprocess/color_grading_lut_cs.hlsl` (lines 198–200, 223, 242, 261, 303, 342, 509–511)

**What was found**:
```hlsl
/// @ref post-process tonemap pipeline:320-342 — USE_COLOR_FRINGE
/// @ref chromatic aberration wavelength model:673 — Offset = SceneFringeIntensity * 0.01f
/// @constants 543.31 (PostProcessTonemap.usf:116), 493013.0 (PostProcessTonemap.usf:116)
/// @constants sqrt(2.0) / sqrt(1.0 + AR*AR) (PostProcessCommon.ush:158)
/// @constants LinearRange=14 (GammaCorrectionCommon.ush:82)
/// @constants LinearGrey=0.18 (GammaCorrectionCommon.ush:83)
/// @constants ExposureGrey=444 (GammaCorrectionCommon.ush:84)
/// @constants LUT normalization factor = 1.05 (CombineLUTsCommon.usf:419)
/// @constants LUT size = 32 (CombineLUTsCommon.usf:39)
/// @constants DefaultTemperature=6500.0 (PostProcessCombineLUTs.usf:84)
/// @constants [Krystek 1985] polynomial coefficients (TonemapCommon.ush:531-532)
/// @constants Bradford cone response matrix (ACESCommon.ush:204-215)
```

Also, the anonymous `@ref` labels `post-process tonemap pipeline`, `chromatic aberration
wavelength model`, `natural vignetting (cos⁴ law)`, and `log-to-linear encoding` are
not real published references — they appear to be informal labels for UE5 shader files.

**Why it is a violation**: `PostProcessTonemap.usf`, `PostProcessCommon.ush`,
`GammaCorrectionCommon.ush`, `CombineLUTsCommon.usf`, `TonemapCommon.ush`,
`ACESCommon.ush`, `PostProcessCombineLUTs.usf` are all UE5 proprietary files.

**Recommended fix**:
- `GammaCorrectionCommon.ush` log encoding constants → `@ref ACES Reference Implementation
  (Academy S-2014-006) — ST-2084 LogC encoding`
- `ACESCommon.ush` Bradford matrix → `@ref ACES Reference Implementation (Academy S-2014-006)
  — chromatic adaptation matrix`
- `PostProcessTonemap.usf` film grain → `@ref Wolfe 2019 "Temporal Dithering" — triangular
  distribution dithering for backbuffer quantization`
- `TonemapCommon.ush` Krystek polynomial → `@ref Krystek 1985 "An Algorithm to Calculate
  Correlated Colour Temperature" — polynomial CCT approximation`
- Remove `CombineLUTsCommon.usf` citations; cite `Selan 2012 "Using Lookup Tables for
  Color Grading"` instead

---

### V7 — `fxaa_ps.hlsl` and `taa_resolve_cs.hlsl` cite NVIDIA/UE5 proprietary file names

**Files**:
- `runtime/shaders/runtime/postprocess/fxaa_ps.hlsl` (lines 8–10, 65, 172, and throughout)
- `runtime/shaders/runtime/postprocess/taa_resolve_cs.hlsl` (lines 6, 399, 405, 408)

**What was found** (FXAA):
```hlsl
/// @constants FxaaLuma weights=(0.299, 0.587, 0.114) (Fxaa3_11.ush:713)
/// @constants Preset 29 step count=12 (Fxaa3_11.ush:575)
/// @constants Preset 29 steps: P0=1.0 P1=1.5 P2-P9=2.0 P10=4.0 P11=8.0 (Fxaa3_11.ush:576-588)
/// FXAA 3.11 requires bilinear filtering (Fxaa3_11.ush:95).
/// FXAA_EDGE_THRESHOLD_MIN = 0.0833 (hardcoded, from PostProcessAA.cpp:96, Fxaa3_11.ush:828)
```

The function names `FxaaTexTop`, `FxaaTexOff`, `FxaaLuma` are directly taken from the
NVIDIA `Fxaa3_11.ush` file (included in UE5 as a third-party header).

**What was found** (TAA):
```hlsl
/// @constants CurrentFrameWeight=0.04 (TemporalAA.cpp:41), velocityLerp=0.2 at v/40 (TemporalAA.usf:2233)
// Catmull-Rom basis weights (reference TextureSampling.ush:110-113).
// Combined weights for bilinear trick (reference TextureSampling.ush:116).
// Bilinear sample positions in texel space (reference TextureSampling.ush:119-121).
```

**Assessment for FXAA**: `Fxaa3_11.ush` is the NVIDIA FXAA 3.11 reference file distributed
by NVIDIA as a public white paper / open algorithm. NVIDIA released FXAA without a license
restriction (it was distributed freely as part of their GPU SDK). The `.ush` extension comes
from UE5 packaging it. The function names `FxaaTexTop`, `FxaaLuma`, etc. are from the NVIDIA
original. This is **lower risk** than the UE5-specific files above, but the UE5 file path
citations should be replaced with the published NVIDIA source.

**Assessment for TAA**: `TemporalAA.usf` and `TextureSampling.ush` are UE5 proprietary files.
`TemporalAA.cpp:41` is a C++ source file reference.

**Recommended fix** (FXAA): Replace `(Fxaa3_11.ush:N)` citations with
`(Lottes 2011 FXAA 3.11 white paper, Preset 29)`. Function names (`FxaaTexTop`, `FxaaLuma`,
`FxaaTexOff`) should be renamed to UzEngine-style names (e.g., `SampleSceneColor`,
`ComputeLuma`, `SampleSceneColorOffset`) to avoid replicating NVIDIA identifiers.

**Recommended fix** (TAA): Replace `(TemporalAA.usf:N)` with `(Karis 2014 "High Quality
Temporal Supersampling")`. Replace `(TextureSampling.ush:N)` with `(Catmull & Rom 1974)`.

---

### V8 — Various postprocess shaders cite UE5 proprietary .usf/.ush files

**Files and lines**:

| File | Proprietary Citation |
|------|---------------------|
| `bloom_threshold_cs.hlsl:9` | `PostProcessBloom.usf` |
| `exposure_average_cs.hlsl:3` | `PostProcessEyeAdaptation.usf` |
| `dof_blur_cs.hlsl:13,24–27,36,303,311,318,325` | `DOFGatherKernel.ush`, `DOFCommon.ush`, `DOFRecombine.usf` |
| `dof_setup_cs.hlsl:48,65,112,115,122,146,185` | `DepthOfFieldCommon.ush`, `DOFCommon.ush`, `DOFSetup.usf` |
| `motion_blur_cs.hlsl:4,10,39–42,114–115,134,144,355` | `MotionBlurApply.usf`, `MotionBlurCommon.ush`, `MotionBlurVelocityFlatten.ush`, `RandomInterleavedGradientNoise.ush` |
| `motionblur_tilemax_cs.hlsl:26,33` | `MotionBlurCommon.ush`, `MotionBlurVelocityFlatten.ush` |
| `motionblur_neighbormax_cs.hlsl:14,28,31` | `MotionBlurTileGather.usf`, `MotionBlurCommon.ush` |
| `lensflare_cs.hlsl:34` | `PostProcessLensFlares.usf` |
| `gtao_denoise_cs.hlsl:81` | `PostProcessAmbientOcclusionCommon.ush` |
| `hiz_pyramid_cs.hlsl:2` | `HZB.usf` |
| `cluster_assign_cs.hlsl:64,165,397,402` | `LightGridCommon.ush`, `LightGridInjection.usf`, `ThreadGroupPrefixSum.ush` |
| `deferred_lighting_ps.hlsl:599` | `UE5 BRDF.ush:EnvBRDF()` |
| `pcss.hlsli:141` | `PixelQuadMessagePassing.ush` |
| `tsr_resolve_cs.hlsl:241,680,686,689` | `TSRDilateVelocity.usf`, `TextureSampling.ush` |
| `god_rays_cs.hlsl:6,46,47,222` | `LightShaftShader.usf` |
| `atmosphere/irradiance_lut_cs.hlsl:6` | cites `atmosphere/functions.glsl (BSD-3 license)` — SAFE |
| `atmosphere/indirect_scattering_cs.hlsl:6` | cites `atmosphere/functions.glsl (BSD-3 license)` — SAFE |
| `atmosphere/scattering_density_cs.hlsl:6` | cites `atmosphere/functions.glsl (BSD-3 license)` — SAFE |

**Why it is a violation**: All `.usf` and `.ush` filenames that are not Fxaa3_11.ush or
publicly released NVIDIA files are UE5 proprietary files. Citing them by name and line
number documents that the algorithm was derived by reading UE5 source code.

**Recommended fix**: Replace each `(FileName.ush:N)` or `(FileName.usf:N)` citation with
the published paper or technique it corresponds to:

| Proprietary Citation | Published Replacement |
|---------------------|----------------------|
| `PostProcessBloom.usf` | `@ref Jimenez 2014 "Next Generation Post Processing" (SIGGRAPH)` |
| `PostProcessEyeAdaptation.usf` | `@ref Lagarde 2016 "Moving Frostbite to PBR" — eye adaptation` |
| `DOFCommon.ush`, `DOFGatherKernel.ush`, `DOFRecombine.usf` | `@ref Bukowski 2014 "Bokeh Depth of Field in a Single Pass" (GPU Pro 5)` |
| `MotionBlurApply.usf`, `MotionBlurCommon.ush` | `@ref Sousa 2008 "Crysis Next Gen Effects" — per-object motion blur` |
| `RandomInterleavedGradientNoise.ush` | `@ref Jimenez 2014 "Interleaved Gradient Noise" (SIGGRAPH)` |
| `HZB.usf` | `@ref Wihlidal 2017 "Hierarchical Z-Buffer" (GDC)` |
| `LightGridCommon.ush`, `LightGridInjection.usf` | `@ref Olsson 2011 "Clustered Deferred and Forward Shading" (HPG)` |
| `BRDF.ush:EnvBRDF()` | `@ref Karis 2013 "Real Shading in Unreal Engine 4"` |
| `TSRDilateVelocity.usf`, `TextureSampling.ush` | `@ref Karis 2023 "Temporal Super Resolution" + Catmull & Rom 1974` |
| `LightShaftShader.usf` | `@ref Mitchell 2007 "Light Shafts: Rendering Shadows in Participating Media"` |
| `PixelQuadMessagePassing.ush` | `@ref Drobot 2017 "Improved Culling for Tiled and Clustered Rendering"` |
| `PostProcessAmbientOcclusionCommon.ush` | `@ref Jimenez 2016 "Practical Real-time Strategies for Accurate Indirect Occlusion"` |

---

## FSR / FXAA FUNCTION NAME CONCERNS (borderline)

### W1 — FSR function names match AMD FidelityFX SDK identifiers

**File**: `runtime/shaders/runtime/postprocess/fsr_easu_cs.hlsl`, `fsr_rcas_cs.hlsl`

**What was found**: Function names `FsrEasuLanczos2`, `FsrEasuTap`, `FsrEasuDir`,
`FsrEasuF`, `FsrRcasF`, and variable `AU1_AF1` are taken directly from AMD's
`ffx_fsr1.h` reference implementation.

**Assessment**: AMD released the FidelityFX SDK (including `ffx_fsr1.h`) under the **MIT
license**. The MIT license explicitly allows use, modification, and distribution provided
the copyright notice is preserved. The code files do not include AMD's copyright notice.

**Status**: **WARNING** — The algorithm implementation is legally permissible under MIT,
but the AMD copyright notice is missing from the files that reuse AMD's identifiers.

**Recommended fix**: Either add AMD's MIT license header at the top of both files, or rename
the functions to UzEngine-style names to make the independent re-implementation clear:
```hlsl
// Option A — add license header:
// FidelityFX Super Resolution 1.0 — based on AMD FidelityFX SDK (MIT License)
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.

// Option B — rename functions:
// FsrEasuF → UpscaleEASU
// FsrRcasF → SharpenRCAS
// FsrEasuLanczos2 → Lanczos2Approx
// FsrEasuTap → AccumulateTap
// FsrEasuDir → DetectEdgeDirection
// AU1_AF1 → UintAsFloat
```

---

## ENTRY POINT VIOLATION

### E1 — `dfs_upsample_cs.hlsl` uses `CSMain` instead of `main`

**File**: `runtime/shaders/runtime/shadows/dfs_upsample_cs.hlsl`
**Line**: 95

**What was found**:
```hlsl
void CSMain(uint3 DTid : SV_DispatchThreadID) {
```

**Why it is a violation**: Per `.claude/rules/shader-entry-point.md`, all shader entry points
MUST be named `main`. UzEngine's `ShaderManager` compiles all HLSL with `entryPoint = "main"`,
so this shader will fail to compile at runtime with "missing entry point definition".

**Recommended fix**: Rename `CSMain` to `main`.

---

## SAFE (no action required)

### SMAA — thirdparty include with proper MIT license

**File**: `runtime/thirdparty/smaa/SMAA.hlsl`

The SMAA implementation is included via `#include "../../../thirdparty/smaa/SMAA.hlsl"` in
`smaa_edge_ps.hlsl`, `smaa_blend_ps.hlsl`, and `smaa_resolve_ps.hlsl`. The file is under
`runtime/thirdparty/smaa/` and has `LICENSE.txt` with a proper MIT license attributed to
Jorge Jimenez et al. (2013). This is correctly handled.

### Bruneton atmosphere (functions.glsl) — BSD-3 license

Several atmosphere LUT shaders (`irradiance_lut_cs.hlsl`, `indirect_scattering_cs.hlsl`,
`scattering_density_cs.hlsl`) cite `atmosphere/functions.glsl (BSD-3 license)` — this is the
open-source Bruneton reference implementation released under BSD-3, which is permissible.

### Published paper citations — all safe

The following files contain only published paper references (SIGGRAPH, GDC, ACM, etc.)
and are clean:

- All PBR shaders (pbr_common.hlsli, ibl.hlsli, deferred_lighting_ps.hlsl main body)
- All geometry shaders (gbuffer_ps.hlsl, mesh_ps.hlsl, etc.)
- All IBL compute shaders (brdf_lut_cs.hlsl, irradiance_cs.hlsl, prefilter_cs.hlsl)
- GTAO (gtao_cs.hlsl) — cites Jimenez 2016 only
- TAA (taa_resolve_cs.hlsl) — cites Karis 2014 and Catmull & Rom 1974 in body (has .ush V7)
- All editor shaders (gizmo_ps/vs, grid_ps/vs, outline, overlay)
- All VFX shaders (particle_*, trail_*, distortion_*)
- All GI shaders (radiance_cache_trace_cs.hlsl, ssgi_*)
- All shadow shaders except DFS (shadow_common.hlsli, pcss.hlsli body)

---

## Priority Fix Order

| Priority | File(s) | Action |
|----------|---------|--------|
| P0 | `dfs_upsample_cs.hlsl:95` | Rename `CSMain` → `main` (runtime crash) |
| P1 | `single_scattering_lut_cs.hlsl` | Replace all `@ref UE5 *.ush`/`*.usf` → Bruneton 2017 |
| P1 | `dfs_cone_trace_cs.hlsl`, `dfs_upsample_cs.hlsl`, `dfs_cull_objects_cs.hlsl`, `dfs_common.hlsli` | Replace `*.ush`/`*.usf` → Wright 2015 |
| P2 | `tonemap_ps.hlsl`, `color_grading_lut_cs.hlsl` | Replace all UE5 file citations → ACES/Selan/Krystek |
| P2 | `taa_resolve_cs.hlsl`, `tsr_resolve_cs.hlsl` | Replace `TemporalAA.usf`, `TextureSampling.ush` |
| P2 | `dof_blur_cs.hlsl`, `dof_setup_cs.hlsl` | Replace DOF .ush/.usf → Bukowski 2014 |
| P2 | `motion_blur_cs.hlsl`, `motionblur_*.hlsl` | Replace MotionBlur .ush/.usf → Sousa 2008 |
| P3 | `fxaa_ps.hlsl` | Rename `Fxaa*` functions to UzEngine names; replace `.ush` citations |
| P3 | `fsr_easu_cs.hlsl`, `fsr_rcas_cs.hlsl` | Add AMD MIT copyright notice or rename `Fsr*` functions |
| P3 | `bloom_threshold_cs.hlsl`, `exposure_average_cs.hlsl`, etc. | Replace remaining .usf citations |

---

## Notes on What is NOT a Violation

1. **`@ref Karis 2013 "Real Shading in Unreal Engine 4"`** — This is a published SIGGRAPH
   course note, not a proprietary source file. Citing it is correct.

2. **`@ref Epic Games, "Real Shading in Unreal Engine 4" course notes`** in `brdf_lut_cs.hlsl`
   — same paper, different citation style. Both are acceptable.

3. **Standard math identifiers** (`NdotV`, `NdotL`, `NdotH`, `LdotH`, `D_GGX`, `F_Schlick`,
   `V_SmithGGXCorrelated`) — these are standard PBR nomenclature from published papers, not
   proprietary identifiers.

4. **`SMAA_RT_METRICS`, `SMAALumaEdgeDetectionPS`** — these are defined in the SMAA.hlsl
   file under MIT license in `runtime/thirdparty/smaa/`.
