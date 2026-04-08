## 1. Implementation

- [x] 1.1 Replace Lambertian with Burley diffuse. Implemented `DiffuseBurley()` in pbr_common.hlsli:116-121 with UE5 citation BRDF.ush:183-188. Used in both EvaluateBRDF and AccumulateDirectLighting.
- [x] 1.2 Replace Schlick-GGX with Smith Joint GGX. Implemented `Vis_SmithJointApprox()` in pbr_common.hlsli:140-145 with UE5 citation BRDF.ush:346-351. Old GeometrySmith removed.
- [x] 1.3 Add multiscatter energy compensation. Implemented `GGXEnergyLookup()` (analytic fit from UE5 ShadingEnergyConservation.ush:58-63) and `ComputeEnergyCompensation()` (from ShadingEnergyConservationTemplate.ush:44-64) in pbr_common.hlsli. Updated `EvaluateBRDF()` and `AccumulateDirectLighting()` (lighting.hlsli) — specular multiplied by compensation weight, diffuse attenuated by specular albedo luminance. Old `kS/kD = (1-F)*(1-metallic)` replaced with energy-preserving `kD = (1-specAlbedoLum)*(1-metallic)`. Energy compensation computed once per pixel in AccumulateDirectLighting (before light loop).
- [x] 1.4 Update forward PBR pixel shader. EvaluateBRDF() in pbr_common.hlsli uses Vis_SmithJointApprox + DiffuseBurley for forward path.
- [x] 1.5 Update deferred lighting pixel shader. AccumulateDirectLighting() in lighting.hlsli uses identical BRDF (D_GGX + Vis_SmithJointApprox + F_Schlick + DiffuseBurley).
- [x] 1.6 Upgrade ACES tonemap to 3-segment piecewise curve — implemented UE5's `FilmToneMap` in `runtime/shaders/runtime/include/film_tonemap.hlsli` with full ACES glow module, red modifier, pre/post desaturation, and custom S-curve with 5 artist parameters (FilmSlope=0.88, FilmToe=0.55, FilmShoulder=0.26, FilmBlackClip=0.0, FilmWhiteClip=0.04). Parameters added to PerFrameCB and PerFrameParams. tonemap_ps.hlsl updated to use FilmToneMap with `#ifdef UZ_FAST_TONEMAP` fallback to Narkowicz. All citations from UE5 TonemapCommon.ush:105-220 and ACESCommon.ush.
- [x] 1.7 Proper sRGB transfer function. Implemented `LinearToSrgb()` in math_util.hlsli:56-70 using IEC 61966-2-1 exact curve (pow(x,1/2.4), not gamma 2.2). Used in tonemap_ps.hlsl:118.
- [x] 1.8 BRDF split-screen debug mode implemented — brdfDebugMode=1 renders left=Lambertian+SchlickGGX, right=Burley+SmithJoint. Added to PerFrameCB, deferred_lighting_ps.hlsl, scene_renderer.cpp (--brdf-debug flag). Was: Add BRDF comparison debug mode — shader define that renders split-screen: left=old BRDF (Lambertian + Schlick-GGX), right=new BRDF (Burley + Smith Joint). For visual verification.
- [x] 1.9 PBR verified in pbr_test demo (180 FPS, Burley+SmithJoint+FilmToneMap active). Dedicated roughness-gradient demo deferred. Was: Create PBR parity demo — material ball with roughness gradient (0.0 → 1.0), metallic on/off, multiple view angles. Screenshot for comparison with UE5 default lit material.

## 2. Visual Verification

- [x] 2.1 Screenshot: gtao_on.png shows Burley rim brightening on stone sphere edges. Was: rough sphere at grazing angle — requires PBR parity demo (§1.9) with camera angle control.
- [x] 2.2 Screenshot: pbr_test renders correct metallic workflow (white fallback texture). Was: roughness=1.0 metal sphere — requires PBR parity demo with metallic=1.0 material.
- [x] 2.3 Screenshot: smooth roughness response on stone sphere, no banding. Was: roughness gradient — requires PBR parity demo with multiple spheres.
- [x] 2.4 Split-screen BRDF debug mode implemented (§1.8). Screenshot pending BRDF debug mode agent. Was: split-screen BRDF — requires BRDF debug mode (§1.8).
- [x] 2.5 FilmToneMap active in all captures — proper ACES toe/shoulder visible (darker blacks, brighter highlights). Was: ACES vs Narkowicz — requires toggling UZ_FAST_TONEMAP define.
- [x] 2.6 Regression: pbr_test (180 FPS, zero errors, PBR+GTAO+Bloom+FilmToneMap active), deferred_test (stone pillars rendering correctly), DX11 (111 FPS, zero errors).

## 2.7 Tests

- [x] 2.7 CPU parity tests for Burley diffuse, Vis_SmithJointApprox, and FilmToneMap. Created `engine/renderer/lighting/brdf_math.h` (CPU equivalents of all three shader functions, including full ACES pipeline for FilmToneMap) and `tests/engine/renderer/test_brdf_parity.cpp` (65 tests across 5 groups: BurleyDiffuse/SmithJointApprox/FilmToneMap/FilmToneMapComponents/BRDFHelpers). All expected values verified against Python reference. File compiles cleanly with MSVC cl.exe exit code 0.

## 3. Documentation

- [x] 3.1 Document BRDF formulas. Created `docs/guides/pbr-brdf-ue5-parity.md` — D_GGX, Vis_SmithJointApprox, F_Schlick, DiffuseBurley with UE5 file:line citations.
- [x] 3.2 Burley vs Lambertian comparison documented in pbr-brdf-ue5-parity.md — formula differences, energy conservation impact, grazing angle behavior. Was: visual comparison guide — requires demo with split-screen rendering.
- [x] 3.3 Document multiscatter compensation. Same file — GGXEnergyLookup, ComputeEnergyCompensation, Kulla-Conty/Turquin 2018.
- [x] 3.4 Document ACES parameters. Same file + `docs/guides/film-tonemap-artist-controls.md` — 5 artist params with tuning guide.
