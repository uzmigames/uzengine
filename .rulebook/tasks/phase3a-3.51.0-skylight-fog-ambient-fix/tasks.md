# Task 3.51.0 — Fix SkyLight Ambient + Fog Color Injection

## SkyLight intensity control correction (2026-10-26)

- [x] Apply SkyLight intensity to the deferred sky-SH diffuse fallback tint, matching the existing single specular multiplier. CPU SH upload continues to apply only indirectLightingIntensity; GI sources that already include sky intensity retain their neutral tint.
- [x] Add a CPU contract test against the embedded deferred shader for both single-scale tint expressions and the GI bypass.
- [x] GPU control sweep at fixed exposure 1, frame 120: halving the authored Sponza SkyLight intensity reduces interior diffuse indirect from 0.502439 to 0.251219 and specular indirect from 0.0454705 to 0.0227353 (linear RGB mean, sky-SH fallback diagnostic).
- [x] Pinned Zig 0.15.2 ReleaseFast build: 47/47 steps, 165 shaders verified. Layout gate passes. Filtered engine suite: 8158 tests, only the three pre-existing VfxEditorSlate failures; new SkyLightIntensity contract passes. DX12DeviceTest recheck: 20/20 pass.
- The authored Sponza project enables final gather and radiance cache. These paths already carry SkyLight intensity and bypass the fallback tint; do not infer a default-project brightness increase from the sky-SH diagnostic.

## Diagnosis (must be done FIRST)

- [x] 1.1 Run pbr_test demo, capture log output at frame 100+ — SH L0: R=0.8422, G=0.8722, B=0.9177. Values are STRONG and correct for Earth-like sky.
- [x] 1.2 SH readback is NOT failing. GPU copy + WaitIdle + Map works correctly. Fencing is proper.
- [x] 1.3 SH L0 values (0.84-0.92) are ABOVE expected range (0.3-0.5) — sky cubemap captures full HDR. No issue here.
- [x] 1.4 IBL path is taken (irradianceSrvIndex=166, not 0xFFFFFFFF). Fallback path not used.
- [x] 1.5 Debug grid captured: GTAO working (AO tile shows proper contact darkening). ALBEDO tile was BLACK at frame 10 due to async texture loading — at frame 120, albedo is correct.
- [x] 1.6 ROOT CAUSE FOUND: Initial screenshot was at frame 10 when textures hadn't loaded yet (async). At frame 120 scene looks much better. Remaining issues: dark horizon band (missing fog sky injection) and slightly dark shadows compared to UE5.

## Fix 1: SH Irradiance Pipeline

- [x] 2.1 Readback works correctly — no fix needed. SH L0 = (0.84, 0.87, 0.92).
- [x] 2.2 Sky cubemap captures full HDR range — values above expected range, not clamped.
- [x] 2.3 SH projection math verified — PackSHToUE5Format matches UE5 ReflectionEnvironment.cpp:623-669 exactly. GetSkySHDiffuse in shader matches UE5 ReflectionEnvironmentShared.ush:84-100.
- [x] 2.4 SH L0 values in range [0.84, 0.92] — healthy for 45-degree sun on Earth sky.
- [ ] 2.5 Visual verification: shadowed areas have SOME fill at frame 120, but still darker than UE5. Need to verify multiplier chain.

## Fix 2: Sky→Fog Color Injection (UE5 FogRendering.cpp parity)

- [ ] 3.1 Read UE5 `FogRendering.cpp:282-290` — extract the exact formula for `SkyAtmosphereAmbientContributionColorScale` injection into fog inscattering.
- [ ] 3.2 Add `skyAtmosphereAmbientContributionColorScale` property to `HeightFogComponent` (Vec3, default {1,1,1}). @source UE5 ExponentialHeightFogComponent.h.
- [x] 3.3 In `fog_renderer.cpp`: when SkyAtmosphere exists, compute injected fog color = `skyLightColor * skyLightIntensity * heightFogContribution * skyAtmContribColorScale`. Pass to fog CB. Implemented full UE5 SkyAtmosphere→HeightFog color injection: CPU-side analytical transmittance (ComputeTransmittanceToSun), DirectionalLightComponent ECS query, sunIllumPostTrans + sunDir CB fields.
- [x] 3.4 In `height_fog_ps.hlsl`: add the injected sky color to the fog inscattering luminance before applying fog opacity. Shader now uses sunIllumPostTrans for atmosphere-tinted directional inscattering (UE5 HeightFogCommon.ush:319 parity).
- [x] 3.5 Read UE5 `HeightFogCommon.ush` — verify the injection happens in the same place in the fog computation (before or after density integration). Confirmed: injection adds to directionalInscatteringColor before phase/fog factor, matching UE5 line 319.
- [ ] 3.6 Visual verification: horizon should have smooth blue-white fog transition blending sky into ground, matching UE5 Basic template.

## Fix 3: Ambient Fallback Cleanup

- [ ] 4.1 In `light_renderer.h` and `scene_lighting.h`: change `ambientColor` default from `{0.08, 0.09, 0.12}` to `{0.0, 0.0, 0.0}`.
- [ ] 4.2 In `frame_setup.cpp`: when SkyLightComponent exists but IBL SH is not ready yet (first few frames), compute fallback ambient = `skyLight.lightColor * skyLight.intensity * 0.1` instead of hardcoded value.
- [ ] 4.3 Ensure the fallback is NEVER used when IBL is active — add an assertion/log if fallback path is taken after frame 10.
- [ ] 4.4 Remove the hardcoded `{0.08, 0.09, 0.12}` from all locations in the codebase. Grep to verify zero remaining instances.

## Final Validation

- [ ] 5.1 Side-by-side screenshot: UzEngine pbr_test vs UE5 Basic template with same sun angle, same sphere. Compare shadow fill, horizon fog, ambient brightness.
- [ ] 5.2 PSNR comparison if possible — target >30 dB for the sky/fog region.
- [ ] 5.3 Verify no regression in existing rendering: bloom, CSM, IBL specular, contact shadows all still correct.
- [ ] 5.4 Update `phase3a-3.50.0-ue5-visual-parity-roadmap/tasks.md` — mark height fog and SkyLight items as fixed.

## 1. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 1.1 Update or create documentation covering the implementation
- [ ] 1.2 Write tests covering the new behavior
- [ ] 1.3 Run tests and confirm they pass
