## 1. Implementation

- [x] 1.1 Implement Hi-Z mip chain generation — `hiz_pyramid_cs.hlsl` compute shader (existed), plus `hiz_pyramid_pass.h/.cpp` render graph pass, `depth_prepass.h/.cpp` depth-only prepass, PipelineLibrary `GetDepthPrepass()`/`GetHiZPyramid()`, SceneRenderer wiring with per-mip R32_FLOAT textures and UAV barriers. Dispatches per-mip with barrier between levels.
- [x] 1.2 Implement SSR ray trace compute shader (`ssr_trace_cs.hlsl`) — Hi-Z ray march using UE5 SSRT pipeline: InitScreenSpaceRayFromWorldSpace, CastScreenSpaceRay with batched 4-sample depth comparisons, linear segment intersection refinement, screen-edge vignette, roughness fade. All constants cited from UE5 SSRTRayCast.ush and SSRTReflections.usf. 8 root constant DWORDs, PerFrameCB at b1, bindless+SM5 paths.
- [x] 1.3 Implement binary refinement in trace shader — Uses UE5's linear segment intersection (DepthDiff0/(DepthDiff0-DepthDiff1)) for sub-step precision, matching SSRTRayCast.ush:602-606. UE5 has binary search code (#if 0) but uses the linear interpolation path by default.
- [x] 1.4 Implement SSR resolve shader (`ssr_resolve_cs.hlsl`) — Reads trace result, applies Schlick Fresnel weighting with metallic-aware F0, roughness fade, intensity scaling. Outputs rgb=Fresnel-weighted reflection, a=blend weight for IBL compositing. Compute shader (not PS) matching UE5 compute path.
- [x] 1.5 Implement roughness cutoff — GetRoughnessFade using UE5's RoughnessMaskScale formula (-2/MaxRoughness, bias +2). Smooth transition near threshold. Both trace and resolve shaders early-out when fade <= 0.
- [x] 1.6 Implement IBL fallback blend — `specularIBL = lerp(specularIBL, ssrData.rgb, ssrData.a)` in deferred_lighting_ps.hlsl. SSR resolve outputs Fresnel-weighted reflection in rgb + blend weight in alpha. Lighting shader samples via ssrSrvIndex (UINT32_MAX = no SSR).
- [ ] 1.7 Implement SSR temporal filtering (`ssr_temporal_cs.hlsl`) — reproject previous SSR using velocity buffer, blend with current frame. Reject history when velocity high or reflection direction changed significantly.
- [x] 1.8 Implement depth thickness test — Built into CastScreenSpaceRay via CompareTolerance: `abs(depthDiff + tolerance) < tolerance` rejects hits where ray Z is too far from scene depth. Tolerance is slope-scaled per UE5 SSRTRayCast.ush:118 (SlopeCompareToleranceScale=4.0, scaled by 1/NumSteps per step).
- [ ] 1.9 Add SSRPassDesc to PostProcessStack — enabled/disabled, quality (Off/Low/Medium/High/Epic), maxRoughness, intensity, maxSteps, binarySteps, fadeStart, fadeEnd, thickness.
- [x] 1.10 Register SSR passes in render graph — SSR_Trace + SSR_Resolve compute passes wired after DFS and before lighting. PipelineLibrary gains GetSsrTrace()/GetSsrResolve(). SceneRenderer creates RGBA16F trace+resolve textures with SRV/UAV, manages lifecycle in Init/Resize/Shutdown. PerFrameCB gains ssrSrvIndex. Shader entry points changed from CSMain to main. HiZ depth buffer used as mip-0 source for trace.
- [ ] 1.11 Create SSR demo — reflective floor plane (roughness=0.05), various objects above it, camera showing clear reflections. Toggle SSR on/off, roughness slider.
- [ ] 1.12 Fix SSR resolve double-application of roughnessFade and intensity — trace pass already applies these (lines 568-569), resolve applies again (lines 231-232). UE5 applies only once (SSRTReflections.usf:401-402). Remove from resolve. **CRITICAL: reflections are intensity^2.** See `docs/analysis/rendering/deep-audit-taa-ssr.md` Issue 20.
- [ ] 1.13 Remove Fresnel weighting from SSR resolve — UE5 does NOT apply Fresnel in SSR pass; it applies EnvBRDF during compositing (SSRTReflections.usf:322). Current F0=lerp(0.04,0.8,metallic) uses invented constant 0.8. Remove Fresnel from resolve, output raw color*vignette. Fix alpha blend weight. **CRITICAL.** See audit Issue 19.
- [ ] 1.14 Fix RayDepthScreen computation — uses world-Z offset instead of view-Z offset for compare tolerance. UE5 uses ViewToClip (SSRTRayCast.ush:93-94). See audit Issue 13.
- [ ] 1.15 Add ray behind-camera clamp — UE5 clamps ray distance to prevent going behind camera (SSRTRayCast.ush:83). Missing in UzEngine. See audit Issue 11.
- [ ] 1.16 Implement SSR hit reprojection — UE5 samples PREVIOUS frame scene color at reprojected hit UV via ClipToPrevClip+velocity (ReprojectHit, SSRTRayCast.ush:696-714). UzEngine samples current frame. **HIGH impact.** See audit Issue 16.

## 2. Visual Verification

- [ ] 2.1 Screenshot: reflective floor with reflected cubes/spheres — clear mirror-like reflection
- [ ] 2.2 Screenshot: roughness gradient — smooth=sharp reflection, rough=blurred, very rough=IBL only
- [ ] 2.3 Screenshot: screen edge fade — reflections near edges fade smoothly, no hard cutoff
- [ ] 2.4 Screenshot: SSR OFF vs ON — clear difference on reflective surfaces
- [ ] 2.5 Screenshot: moving objects reflected in real-time (not static like cubemap)
- [ ] 2.6 Regression: existing demos still work

## 3. Documentation

- [ ] 3.1 Document SSR parameters with UE5 equivalents
- [ ] 3.2 Add Hi-Z ray marching algorithm overview
- [ ] 3.3 Document SSR-IBL fallback blending strategy
