# CHANGELOG v0.2.35 — Rendering Parity Audit Corrections

**Date:** 2026-03-18

## Critical Fixes

### BRDF Foundation
- **VisibilitySmithGGX input corrected** — now receives `Pow4(roughness)` instead of `roughness²`. This was the most impactful bug: ALL specular highlights on every surface were too bright and too narrow. Fixed in `pbr_common.hlsli` and `lighting.hlsli`. @source UE5 ShadingModels.ush:215
- **F_Schlick F90 micro-occlusion** — `F90 = saturate(50.0 * F0.g)` replaces hardcoded 1.0. Prevents over-bright grazing reflections on low-F0 materials. @source UE5 BRDF.ush:382
- **NdotV clamping** — `saturate(abs(NoV) + 1e-5)` replaces `max(dot, 0.001)`. Handles back-facing normals correctly. @source UE5 ShadingModels.ush:204

### TAA
- **Motion vector support** — `ComputeMotionVector()` stub replaced with velocity buffer read. TAA now correctly reprojects history using per-pixel motion vectors from the velocity pass.
- **Bicubic history sampling** — single bilinear replaced with 5-tap Catmull-Rom filter for sharper temporal accumulation. @source UE5 TextureSampling.ush:101-186

### SSR
- **Removed invented Fresnel** — deleted `F0 = lerp(0.04, 0.8, metallic)` from resolve. SSR now outputs unweighted scene color. Fresnel weighting deferred to IBL compositing via EnvBRDF.
- **Fixed double intensity** — `roughnessFade * intensity` removed from resolve (already applied in trace).
- **Fixed SSR/IBL compositing** — EnvBRDF now applied ONCE to the combined SSR+IBL result with F90 micro-occlusion.

## High Fixes

- **Vignette in HDR space** — moved from LDR (after tonemap) to HDR (before exposure/tonemap). Added VignetteSpace aspect ratio correction. @source UE5 PostProcessTonemap.usf:515
- **Auto-exposure log2 adaptation** — rewrote from linear-space single exponential to log2-space dual exponential/linear model with 1.5 f-stop transition distance. @source UE5 PostProcessHistogramCommon.ush:176-219
- **Per-light SpecularScale/DiffuseScale** — wired the declared-but-unused scale fields in point and spot light loops via new `EvaluateBRDFScaled()`. @source UE5 DeferredLightingCommon.ush:447-448
- **IBL EnvBRDF F90** — added `saturate(50*F0.g)` micro-occlusion to the bias term.
- **Motion blur neighbor max** — gather shader now reads neighbor max tiles for per-tile early-out. Packed width/height into single DWORD to fit neighborMaxSrvIndex in 8 DWORDs.

## Medium Fixes

- **GTAO quality** — increased NUM_TAPS from 4 (very low) to 8 (UE5 medium quality).
- **Bloom RGB tint** — per-stage tint changed from scalar float to float3 RGB for colored bloom support.

## Low Fixes

- **Motion blur first-frame guard** — `m_prevViewProjValid` added to prevent reading uninitialized velocity.
- **GTAO denoise kernel** — centered offsets replace asymmetric (0,0)/(2,0)/(0,2)/(2,2) pattern.
- **Karis bloom threshold** — documented as intentional deviation from UE5 (Jimenez 2014 pattern).

## Quality Improvements (pre-audit)

- **16x anisotropic filtering** — enabled across DX12, DX11, and Vulkan backends (was 1x disabled).
- **FXAA 3.11** — replaced 3-tap stub with full Timothy Lottes algorithm (12-step edge search, sub-pixel AA).
- **Mipmap generation** — all material textures now created with full mipchain (CPU box filter). Supports up to 16K textures.

## Files Modified

- `runtime/shaders/runtime/include/pbr_common.hlsli` — BRDF fixes + EvaluateBRDFScaled
- `runtime/shaders/runtime/include/lighting.hlsli` — Vis + F90 fixes
- `runtime/shaders/runtime/postprocess/taa_resolve_cs.hlsl` — velocity + bicubic
- `runtime/shaders/runtime/postprocess/ssr_resolve_cs.hlsl` — remove Fresnel + double intensity
- `runtime/shaders/runtime/postprocess/tonemap_ps.hlsl` — vignette HDR + aspect ratio
- `runtime/shaders/runtime/postprocess/exposure_average_cs.hlsl` — log2 dual adaptation
- `runtime/shaders/runtime/postprocess/motion_blur_cs.hlsl` — neighbor max + packed dims
- `runtime/shaders/runtime/postprocess/gtao_cs.hlsl` — 8 taps
- `runtime/shaders/runtime/postprocess/gtao_denoise_cs.hlsl` — centered kernel
- `runtime/shaders/runtime/postprocess/bloom_upsample_cs.hlsl` — RGB tint
- `runtime/shaders/runtime/postprocess/bloom_threshold_cs.hlsl` — documented deviation
- `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl` — SSR compositing + per-light scales
- `runtime/engine/renderer/frame/frame_postprocess.cpp` — TAA CBV + motion blur constants
- `runtime/engine/renderer/frame/frame_setup.cpp` — motion blur first-frame guard
- `runtime/engine/renderer/passes/postprocess/bloom_pass.cpp` — RGB tint constants
- `runtime/engine/renderer/upload/texture_loader.cpp` — mipmap generation
- `runtime/engine/rhi/dx12/dx12_pso_creation.cpp` — 16x anisotropic
- `runtime/engine/rhi/dx11/dx11_device.cpp` — 16x anisotropic
- `runtime/engine/rhi/vulkan/vulkan_device.cpp` — 16x anisotropic
