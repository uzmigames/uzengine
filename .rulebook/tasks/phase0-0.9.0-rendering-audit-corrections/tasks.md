## 1. CRITICAL — BRDF Foundation (affects every lit pixel)

- [ ] 1.1 Fix Vis_SmithJointApprox input — pass `Pow4(roughness)` as `a2` parameter instead of `roughness * roughness`. In `pbr_common.hlsli`, change the call site in `EvaluateBRDF()` from `Vis_SmithJointApprox(a2, NoV, NoL)` where `a2 = roughness*roughness` to `a2 = Pow4(roughness)`. @source UE5 ShadingModels.ush:215 — `float a2 = Pow4(Material.Roughness)`.
- [ ] 1.2 Fix F_Schlick F90 micro-occlusion — replace hardcoded `F90 = 1.0` with `F90 = saturate(50.0 * SpecularColor.g)` in `F_Schlick()` call in `EvaluateBRDF()`. This prevents over-bright grazing reflections on materials with sub-2% F0. @source UE5 BRDF.ush:382 — `float F90 = saturate(50.0 * GBuffer.SpecularColor.g)`.
- [ ] 1.3 Fix NdotV clamping — replace `max(dot(N, V), 0.001)` with `saturate(abs(dot(N, V)) + 1e-5)` everywhere NdotV is computed (pbr_common.hlsli, deferred_lighting_ps.hlsl). The `abs()` handles back-facing normals. @source UE5 ShadingModels.ush:204 — `NoV = saturate(abs(NoV) + 1e-5)`.
- [ ] 1.4 Visual verification — capture before/after screenshots of PBR spheres with roughness gradient (0.0 to 1.0). The specular highlight shape MUST change visibly (wider at high roughness after fix).

## 2. CRITICAL — TAA Motion Vectors (broken for camera motion)

- [ ] 2.1 Add velocitySrvIndex to TAA root constants — expand TaaConstants struct in frame_postprocess.cpp from 8 to include velocitySrvIndex. Pass `m_gbuffer.velocitySRV`. Update the HLSL cbuffer in taa_resolve_cs.hlsl to match.
- [ ] 2.2 Implement ComputeMotionVector — replace the stub (returns 0,0) with depth-based camera reprojection: reconstruct world pos from depth + invViewProj, reproject through prevViewProj, compute UV delta. Sample velocity buffer at closest-depth neighbor pixel. @source UE5 TemporalAA.usf:2087-2121.
- [ ] 2.3 Add prevViewProj to TAA cbuffer — pass the previous frame's view-projection matrix so the shader can reproject. Currently only jitter offset is passed.
- [ ] 2.4 Implement bicubic history sampling — replace single bilinear `SampleLevel()` with 5-tap Catmull-Rom filter for sharper temporal accumulation. @source UE5 TemporalAA.usf:1840-1880, TextureSampling.ush GetBicubic2DCatmullRomSamples.
- [ ] 2.5 Visual verification — orbit camera around scene, TAA must NOT produce ghosting trails on static objects. Compare with TAA OFF to verify stability.

## 3. CRITICAL — SSR Resolve (incorrect reflections)

- [ ] 3.1 Remove invented Fresnel from ssr_resolve_cs.hlsl — delete the `F0 = lerp(0.04, 0.8, metallic)` and `FresnelSchlick()` application. SSR output should be: `output.rgb = hitColor * vignette`, `output.a = confidence`. Fresnel weighting happens in the deferred lighting pass via EnvBRDF. @source UE5 SSRTReflections.usf:397-402 — no Fresnel in SSR resolve.
- [ ] 3.2 Remove double roughnessFade and intensity — the trace pass (ssr_trace_cs.hlsl:568-569) already multiplies by roughnessFade and intensity. Remove the SECOND application in ssr_resolve_cs.hlsl:231-232. @source UE5 applies these only once at SSRTReflections.usf:401-402.
- [ ] 3.3 Fix blend weight alpha — output `confidence * roughnessFade * ssrIntensity` in alpha (no Fresnel). The lighting pass reads this alpha to lerp between SSR and IBL.
- [ ] 3.4 Update deferred_lighting_ps.hlsl SSR compositing — verify the IBL/SSR blend uses `ssrData.a` as blend factor and applies EnvBRDF Fresnel weighting on the IBL side (not SSR side).
- [ ] 3.5 Visual verification — metallic floor plane should show correct reflection intensity. Compare SSR ON vs OFF — SSR should be similar brightness to IBL, not darker.

## 4. HIGH — Tonemap Vignette Order

- [ ] 4.1 Move vignette to before exposure/tonemap — in tonemap_ps.hlsl, move the `ComputeVignetteMask()` multiply from after FilmToneMap (LDR, step 7) to before exposure (HDR, between steps 1 and 4). Apply as `color *= ComputeVignetteMask(...)` before `color *= exposure`. @source UE5 PostProcessTonemap.usf:515.
- [ ] 4.2 Add VignetteSpace aspect ratio correction — compute `scale = sqrt(2.0) / sqrt(1.0 + aspectRatio * aspectRatio)` and apply `vignettePos *= float2(1.0, aspectRatio) * scale` before the cos⁴ formula. Requires adding aspect ratio to PerFrameCB or computing from screen dimensions. @source UE5 PostProcessCommon.ush:155-159.
- [ ] 4.3 Visual verification — on ultrawide (21:9) viewport, vignette must be circular (not elliptical). On 16:9, compare before/after at vignetteIntensity=0.8 — edges should be darker after fix.

## 5. HIGH — Auto-Exposure Adaptation

- [ ] 5.1 Rewrite temporal adaptation in log2 space — in exposure_average_cs.hlsl, convert prevExposure and targetExposure to log2 before adaptation, adapt in log space, convert back to linear. @source UE5 PostProcessHistogramCommon.ush:192-219.
- [ ] 5.2 Implement dual exponential/linear model — if `abs(logTarget - logPrev) > startDistance` (1.5 f-stops), use linear adaptation; otherwise use exponential with M multiplier. @source UE5 PostProcessHistogramCommon.ush:200-213, PostProcessEyeAdaptation.cpp:491-505.
- [ ] 5.3 Fix speed semantics — clarify SpeedUp vs SpeedDown naming to match UE5 convention. SpeedUp = speed when scene gets BRIGHTER (exposure decreases). @source UE5 PostProcessHistogramCommon.ush:199-200.
- [ ] 5.4 Visual verification — fly camera from dark room to bright outdoor: adaptation must feel smooth and perceptually uniform. Log2 adaptation should NOT accelerate near the target like linear does.

## 6. HIGH — Lighting Per-Light Scales

- [ ] 6.1 Apply SpecularScale and DiffuseScale in deferred lighting — in both point light and spot light evaluation loops in deferred_lighting_ps.hlsl, multiply specular BRDF result by `light.specularScale` and diffuse result by `light.diffuseScale`. @source UE5 DeferredLightingCommon.ush:447-448.
- [ ] 6.2 Fix IBL EnvBRDF micro-occlusion — in the IBL evaluation section, change the EnvBRDF bias term from `F0 * AB.x + AB.y` to `F0 * AB.x + saturate(50.0 * F0.g) * AB.y`. @source UE5 EnvBRDFApprox.
- [ ] 6.3 Visual verification — set a point light with specularScale=0 in the demo. Specular highlight must disappear while diffuse remains.

## 7. HIGH — Motion Blur Neighbor Max

- [ ] 7.1 Add neighborMaxSrvIndex to motion blur root constants — restructure the 8 DWORDs in motion_blur_cs.hlsl to include the neighbor max SRV index. Pack outputWidth and outputHeight into a single uint32 (16 bits each) to free a DWORD, or switch to a CBV.
- [ ] 7.2 Read neighbor max in gather shader — sample the neighbor max texture at `(threadId.xy / 16)` to get the per-tile max velocity. Use it for early-out: if max velocity < 0.5px, skip the tile (passthrough center color). @source UE5 MotionBlurApply.usf:269-285.
- [ ] 7.3 Visual verification — scene with mostly static objects + one moving object: draw call count / GPU time should decrease with neighbor max (most tiles early-out).

## 8. MEDIUM — GTAO Quality

- [ ] 8.1 Increase GTAO_NUM_TAPS from 4 to 8 — in gtao_cs.hlsl, change `#define GTAO_NUM_TAPS 4` to `#define GTAO_NUM_TAPS 8` for UE5 medium quality parity. @source UE5 PostProcessAmbientOcclusion.usf:542 — SHADER_QUALITY 2 uses GTAO_NUMTAPS=8.
- [ ] 8.2 Visual verification — AO should be smoother with less noise. Compare AO-only view (shadow debug grid tile 7) before/after.

## 9. MEDIUM — Bloom Per-Stage RGB Tint

- [ ] 9.1 Change g_StageTint from float to float3 — in bloom_upsample_cs.hlsl, change the scalar tint to a float3 and apply `upsampled *= g_StageTint.rgb`. Update the C++ root constants to pass 3 floats per stage.
- [ ] 9.2 Update BloomSettings — add per-stage float3 tint colors with UE5 defaults. @source UE5 Scene.cpp:426-437 — Bloom1Tint through Bloom6Tint.

## 10. LOW — Minor Fixes

- [ ] 10.1 Fix motion blur first-frame garbage — add `m_prevViewProjValid` to motionBlurActive condition in frame_setup.cpp to prevent motion blur from reading uninitialized velocity on frame 0.
- [ ] 10.2 Fix GTAO denoise kernel asymmetry — change offsets from (0,0),(2,0),(0,2),(2,2) to (-1,-1),(1,-1),(-1,1),(1,1) for centered 4x4 coverage. @source UE5 PostProcessAmbientOcclusion.usf:528-534.
- [ ] 10.3 Document Karis average at bloom threshold as intentional deviation from UE5.

## 11. Visual Verification (full pipeline)

- [ ] 11.1 Screenshot: PBR material ball array (roughness 0.0 to 1.0) — specular highlights must match UE5 reference (wider at high roughness).
- [ ] 11.2 Screenshot: camera orbit with TAA ON — zero ghosting on static objects.
- [ ] 11.3 Screenshot: metallic floor SSR — reflection brightness matches IBL fallback, not darker.
- [ ] 11.4 Screenshot: vignette at 0.8 intensity — circular on all aspect ratios, edges correctly dark.
- [ ] 11.5 Screenshot: dark-to-bright transition — smooth, perceptually uniform exposure adaptation.
- [ ] 11.6 Screenshot: shadow debug grid — GTAO tile shows smoother AO with 8 taps.
- [ ] 11.7 Regression: all existing demos produce correct output, no visual artifacts introduced.

## 12. Documentation

- [ ] 12.1 Update `docs/analysis/rendering/deep-audit-*.md` with fix status for each issue.
- [ ] 12.2 Update CHANGELOG with v0.2.35 entry listing all parity corrections.
- [ ] 12.3 Update `docs/engine/12-rendering/` with corrected BRDF formula documentation.
