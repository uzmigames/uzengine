# Proposal: Rendering Audit Corrections — UE5 5.6 Parity Fixes (Phase 0)

## Why

A deep line-by-line audit of all 20+ rendering shaders and the C++ pipeline against UE5 5.6 source (W:\UE_5.6_Source) revealed 53 issues across 4 audit reports. Of these, 5 are CRITICAL (affect fundamental rendering correctness on every pixel), 5 are HIGH (break specific features), and 10 are MEDIUM (reduce quality).

The most severe finding: `Vis_SmithJointApprox` receives `roughness²` instead of `roughness⁴`, making ALL specular highlights on EVERY surface too bright and too narrow. This single bug affects every lit pixel in the engine.

Other critical issues: TAA has no motion vector support (ghosting on any camera movement), SSR applies an invented Fresnel constant (0.8) and double-applies intensity, vignette is applied in LDR instead of HDR space, and auto-exposure adapts in linear instead of log2 space.

These are not missing features — they are **bugs in existing code** that produce visually incorrect results compared to UE5. Fixing them is higher priority than adding new features because every new feature built on top of broken BRDF/lighting will also be wrong.

Audit reports: `docs/analysis/rendering/deep-audit-*.md` (4 files, ~2500 lines total).

## What Changes

### A. BRDF Fixes (CRITICAL — affects every lit pixel)
1. **Vis_SmithJointApprox input**: Pass `Pow4(roughness)` as `a2` instead of `roughness*roughness`. @source UE5 ShadingModels.ush:215
2. **F_Schlick F90 micro-occlusion**: Use `F90 = saturate(50.0 * SpecularColor.g)` instead of hardcoded 1.0. @source UE5 BRDF.ush:382
3. **NdotV clamping**: Use `saturate(abs(NoV) + 1e-5)` instead of `max(dot, 0.001)`. @source UE5 ShadingModels.ush:204

### B. TAA Fixes (CRITICAL — broken for camera motion)
4. **ComputeMotionVector**: Replace stub with depth-based camera reprojection using prevViewProj + velocity buffer sampling. @source UE5 TemporalAA.usf:2087-2121
5. **Velocity buffer SRV**: Add velocitySrvIndex to TAA root constants and pass m_gbuffer.velocitySRV.
6. **Bicubic history sampling**: Replace bilinear with 5-tap Catmull-Rom. @source UE5 TemporalAA.usf:1840-1880

### C. SSR Fixes (CRITICAL — incorrect reflections)
7. **Remove invented Fresnel**: Delete `F0 = lerp(0.04, 0.8, metallic)` from ssr_resolve_cs.hlsl. SSR output should be unweighted color * vignette * roughnessFade * intensity. @source UE5 SSRTReflections.usf:397-402
8. **Fix double intensity**: Remove roughnessFade and intensity multiplication from resolve pass (already applied in trace). @source UE5 SSRTReflections.usf:401-402
9. **Fix blend weight alpha**: Output confidence (vignette) only, not Fresnel-weighted blend.

### D. Tonemap/Post-Process Fixes (HIGH)
10. **Vignette before tonemap**: Move vignette multiply from LDR (after tonemap) to HDR (before exposure). @source UE5 PostProcessTonemap.usf:515
11. **Vignette aspect ratio**: Add VignetteSpace correction `sqrt(2)/sqrt(1+AR²)`. @source UE5 PostProcessCommon.ush:155-159

### E. Auto-Exposure Fix (HIGH)
12. **Log2-space adaptation**: Rewrite temporal adaptation to work in log2 space with dual exponential/linear model. @source UE5 PostProcessHistogramCommon.ush:192-219

### F. Lighting Fixes (HIGH)
13. **Apply SpecularScale/DiffuseScale**: Wire the per-light scale factors that are declared but never applied. @source UE5 DeferredLightingCommon.ush:447-448
14. **IBL EnvBRDF micro-occlusion**: Add `saturate(50*F0.g)` to the bias term. @source UE5 EnvBRDFApprox (ReflectionEnvironmentShaders.usf)

### G. Motion Blur Fix (HIGH)
15. **Wire neighbor max to gather**: Pass neighborMaxSrvIndex to motion_blur_cs.hlsl and use it for per-tile early-out. @source UE5 MotionBlurApply.usf:269-285

### H. GTAO Quality (MEDIUM)
16. **Increase NUM_TAPS**: Change from 4 (very low) to 8 (medium quality). @source UE5 PostProcessAmbientOcclusion.usf:540-563

### I. Bloom Fix (MEDIUM)
17. **Per-stage RGB tint**: Change g_StageTint from float to float3 for colored bloom. @source UE5 Scene.cpp:426-437

## Impact
- Affected specs: docs/specs/rendering/, docs/engine/12-rendering/
- Affected code: runtime/shaders/runtime/ (12 shader files), runtime/engine/renderer/frame/ (2 cpp files)
- Breaking change: NO — fixes produce MORE correct output, no API change
- User benefit: Visually correct PBR, working TAA, correct SSR reflections, proper exposure adaptation
- Enables: All downstream rendering features built on correct BRDF/lighting foundation
