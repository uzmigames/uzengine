# Task 3.51.0 — Fix SkyLight Ambient + Fog Color Injection

## Diagnosis (must be done FIRST)

- [ ] 1.1 Run pbr_test demo, capture log output at frame 100+ — read "SkyLight SH L0: R=? G=? B=?" values.
- [ ] 1.2 If SH L0 values are zero: trace GPU readback path in `atmosphere_renderer.cpp` — find where the copy/fence/map fails.
- [ ] 1.3 If SH L0 values are non-zero: compare with expected Earth sky SH range (L0 ~0.3-0.5 per channel). If too low, investigate sky cubemap capture brightness.
- [ ] 1.4 Verify in deferred_lighting_ps.hlsl which path executes: IBL path (line 962) or fallback path (line 1032). Add a debug visualization or log to confirm.
- [ ] 1.5 Capture screenshot with `--shadow-debug 1` grid — inspect AO tile to verify GTAO is contributing.

## Fix 1: SH Irradiance Pipeline

- [ ] 2.1 If readback returns zeros: fix the GPU copy → readback buffer → CPU map sequence. Read UE5 `SkyLightComponent.cpp` for reference on how UE5 reads back SH.
- [ ] 2.2 If readback works but values are low: compare sky cubemap capture with UE5. Verify the cubemap is capturing full HDR range (not clamped to [0,1]).
- [ ] 2.3 Verify SH projection math matches UE5 `ComputeSkyEnvMapDiffuseIrradianceCS.usf` — ensure L2 band is correct, not just L0.
- [ ] 2.4 After fix: log SH L0 values, verify they are in range [0.2, 0.8] for a standard Earth sky with sun at 45 degrees.
- [ ] 2.5 Visual verification: shadowed areas should have visible blue-tinted fill light, not black.

## Fix 2: Sky→Fog Color Injection (UE5 FogRendering.cpp parity)

- [ ] 3.1 Read UE5 `FogRendering.cpp:282-290` — extract the exact formula for `SkyAtmosphereAmbientContributionColorScale` injection into fog inscattering.
- [ ] 3.2 Add `skyAtmosphereAmbientContributionColorScale` property to `HeightFogComponent` (Vec3, default {1,1,1}). @source UE5 ExponentialHeightFogComponent.h.
- [ ] 3.3 In `fog_renderer.cpp`: when SkyAtmosphere exists, compute injected fog color = `skyLightColor * skyLightIntensity * heightFogContribution * skyAtmContribColorScale`. Pass to fog CB.
- [ ] 3.4 In `height_fog_ps.hlsl`: add the injected sky color to the fog inscattering luminance before applying fog opacity.
- [ ] 3.5 Read UE5 `HeightFogCommon.ush` — verify the injection happens in the same place in the fog computation (before or after density integration).
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
