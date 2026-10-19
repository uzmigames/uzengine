## Session 1: Clear-Coat BRDF + GBuffer Extension

- [ ] 1.1 Create `include/clearcoat.hlsli` — `struct ClearCoatResult { float3 coatSpecular; float coatFresnel; }`. `ClearCoatResult EvaluateClearCoatBRDF(float3 N, float3 V, float3 L, float coatRoughness, float coatIOR)`: compute half vector H, Fresnel using Schlick approximation with `f0 = ((coatIOR - 1) / (coatIOR + 1))²`, GGX NDF and Smith-G1 visibility function at coat roughness, combine: `coatSpecular = D * G * F / (4 * NdotL * NdotV)`. @ref Kulla & Conty 2017 "Revisiting Physically Based Shading at Imageworks"
- [ ] 1.2 Add Kulla-Conty energy compensation term: `coatFresnel_avg = integrate Fresnel over hemisphere` (approximated via fitted polynomial from Kulla & Conty 2017 Table 1). Multiply base layer by `(1 - coatFresnel_avg)²` to account for energy absorbed in the coat.
- [ ] 1.3 Update GBuffer layout in `gbuffer.h`: add `coatRoughness` (R8_UNORM) and `coatIOR` (R8_UNORM, encoded as `(ior - 1.0) / 2.0` for range [1, 3]) to spare channel. Update `gbuffer_ps.hlsl` to write these for `materialType=4` meshes (zero-fill for other material types).
- [ ] 1.4 Add `ClearCoatMaterial` in `material.h` extending `StandardMaterial` with `float coatRoughness = 0.0f` and `float coatIOR = 1.5f`. Map `materialType=4` in `GetMaterialDefines()` (requires shader variant system task).
- [ ] 1.5 Build + verify: GBuffer pass compiles with new layout. Existing materials write zero to coat channels. No rendering regressions.

## Session 2: Deferred Lighting Integration + Demo

- [ ] 2.1 Add `materialType=4` dispatch path in `deferred_lighting_ps.hlsl` (guarded by `#ifdef CLEARCOAT`): read `coatRoughness` and decode `coatIOR` from GBuffer. Call `EvaluateClearCoatBRDF` for each directional light. Compute attenuated base: evaluate existing `EvaluatePBR` from `brdf.hlsli` multiplied by `(1 - coatFresnel)²`. Add coat and attenuated base contributions.
- [ ] 2.2 Environment lighting for clear-coat: evaluate coat specular from environment map IBL using coat roughness mip. Evaluate attenuated base IBL using base roughness. Both contributions added.
- [ ] 2.3 Demo: create two car-paint materials in an existing demo — Material A: metallic base (metallic=1, roughness=0.3) + smooth coat (coatRoughness=0.05, coatIOR=1.5). Material B: rough diffuse base (metallic=0, roughness=0.8) + smooth wet coat (coatRoughness=0.0, coatIOR=1.33 — water). Apply to two spheres.
- [ ] 2.4 Build + verify via `--screenshot-stdout`: Material A should show dual highlights (sharp coat + broader base metallic). Material B should show a wet-surface appearance with specular coat over matte base. Compare with single-layer PBR via `--flip` to confirm visible difference.

## 1. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 1.1 Update or create documentation covering the implementation
- [ ] 1.2 Write tests covering the new behavior
- [ ] 1.3 Run tests and confirm they pass

## DXR Stage 1: Opt-in Coat over the Existing Base (2026-10-18)

This separately authorized DXR stage does not complete the deferred-renderer
Sessions 1/2 above. There is no design.md in this task. The explicit scope decision
preserves the current base BRDF, including its pre-existing white-furnace gain:
the energy gate is relative, `albedo(layered) - albedo(base) <= F`, not an
absolute `albedo <= 1` claim.

- [x] DXR.1 Dielectric GGX coat extends the existing EvaluatePBR without changing
  its equations. Schlick IOR Fresnel weights the coat reflection. The base is
  multiplied by `(1 - weight*F(NoV)) * (1 - weight*F(NoL))` for entry and exit;
  both factors are in [0,1]. Roughness uses the engine's existing minimum GGX
  alpha, including at roughness zero. No absorption/tint is applied.
- [x] DXR.2 Opt-in `--path-trace --layered` / `r.PathTracer.LayeredMaterial`.
  Stage-1 global controls: `r.PathTracer.Coat.Weight` [0,1] (default 1),
  `.Roughness` [0,1] (default 0.1), `.IOR` [1,3] (default 1.5). Disabled or
  zero-weight coats take the original evaluation, sampling, and PDF branches;
  zero weight consumes no extra random dimension. The pass-private material
  structured-buffer stride grows from 100 to 112 bytes, with coat fields at
  offsets 100/104/108. PathTracerRoot remains 96 bytes / 24 DWORDs; no shared
  graphics CB, GBuffer, or root signature is changed.
- [x] DXR.3 Fresnel-weighted stochastic coat/base choice evaluates both lobe
  densities in the mixture PDF. Continuation weights and light-BVH/emitter MIS
  use that same layered BSDF/PDF. NRD diffuse fractions include base attenuation.
  Active coats bypass base-only ReSTIR reservoirs in favor of ordinary NEE;
  zero coat retains the original ReSTIR path.
- [x] DXR.4 Relative white-furnace proof uses deterministic cosine-hemisphere
  quadrature and the actual C++/HLSL-shared coat functions, with the unchanged
  base equations as reference. 144 cases: NoV {1,0.5,0.1,0.01}, base roughness
  {0.3,0.8,1}, coat roughness {0.25,0.5,1}, metallic {0,1}, weight {0.25,1},
  IOR 1.5; 256x512 integration samples per case. Measured maximum
  `layered-base = -0.000773442`; maximum `layered-base-F = -0.010773443`
  (gate tolerance 0.002). Grazing example: base 1.282903133, layered
  0.586896250, delta -0.696006883, F 0.606870294, integrated coat 0.169691282.
  Maximum absolute layered albedo is 1.142395244: inherited base gain remains
  explicitly out of scope, not hidden or clamped. This is a CPU numerical
  furnace measurement of the shared shader math, not a tone-mapped PNG estimate.
- [x] DXR.5 RTX 5080 captures, all exit 0 and no `compile failed` in run.log:
  `E:/UzmiEngine/layered-stage1/{base,zero,coat}/frames/f_255.png` and
  `default/frames/f_95.png`. Base and coat PNGs were read visually: the coat adds
  a sharp white highlight over the broad red metallic highlight and a small
  white wet-surface highlight on the rough blue dielectric. Maximum brightening
  is 243 RGB code values; the two-pixel silhouette band (740 pixels) brightens
  by mean 4.222522523 / maximum 22 codes. Zero coat versus layered-off:
  maximum code delta 0, mean absolute delta 0; both image means
  37.38274884259259. Default raster versus
  `E:/UzmiEngine/f005-after/frames/f_95.png`: maximum code delta 0.
  Reproduce with `python scripts/rendering/test_layered_bsdf_stage1.py
  --out E:/UzmiEngine/layered-stage1 --case CASE`, CASE =
  coat/base/zero/default/metrics; every render is bounded to 180 seconds.
- [x] DXR.6 `cd runtime && zig build -Doptimize=ReleaseFast`: exit 0;
  151 embedded shaders verified; RTX capture logs confirm RT-PSO creation and
  layered DispatchRays. `npm run test:layouts`: exit 0, Tier A 88/88,
  Tier B 31/31, Tier C 12/12; the existing material layout entry verifies
  the new 112-byte stride and all fields. Six new `LayeredBSDFStage1` tests pass:
  CoatFresnelNormalGrazingAndMatchedIOR, ZeroCoatIsExactBaseAndPDFPassThrough,
  TwoInterfaceCompositionNeverAmplifiesBase, LobeSelectionUsesBothDensities,
  GGXCoatMatchesEngineNormalIncidence, WhiteFurnaceRelativeEnergyBound.
  Full `GTEST_FILTER='-RuntimeViewportTest.*' zig build test
  -Doptimize=ReleaseFast`: 7849 passed (+6 vs 7843), 17 skipped,
  the same 3 pre-existing VfxEditorSlate failures (WorkbenchHasTwoColumns,
  TitleBarReflectsEmitterName, StatusBarBindingsReflectTelemetry), exit 1;
  no new failures or TDR. XML evidence:
  `E:/UzmiEngine/layered-stage1/gtests.xml`, `layered-tests.xml`.

Deferred after stage 1: the deferred-renderer GBuffer/material dispatch, Kulla-Conty fitted
compensation, per-material authoring, coat tint/absorption, layered ReSTIR targets,
full multilayer stacks, thin-film iridescence, and anisotropic coats.

## DXR Stage 2: Clearcoat Absorption Approximation (2026-10-18)

The authorized scope is absorption/tint only on the stage-1 external-direction
composition. It is not a physically refracted stack: neither base directions nor
GGX lobes are refracted. The base BRDF (including D7) remains unchanged.
The transmitted base is multiplied by RGB `exp(-sigmaA * pathLength)` where
`pathLength = thickness/2 * (1/cos(theta_t_v) + 1/cos(theta_t_l))` and Snell gives
`cos(theta_t) = sqrt((IOR*IOR - 1 + NoX*NoX)/(IOR*IOR))`. This stable rearrangement
avoids cancellation at IOR 1 near grazing. Refracted cosines are floored at
1e-6 only for the matched-air grazing limit. Thickness is the normal-incidence
round-trip optical distance in centimeters (twice geometric film thickness);
sigmaA is inverse centimeters. Red-channel absorption removes red light; a
red-looking coat therefore has greater green/blue than red absorption.

Controls, still opt-in via `--path-trace --layered`:
`r.PathTracer.Coat.Thickness` [0,1e6], default 1, and
`.SigmaAR` / `.SigmaAG` / `.SigmaAB` [0,1e6], default 0.
Zero sigmaA, zero thickness, and zero coat retain the stage-1 transmission;
the reflection, lobe selection, mixture PDF, and random dimensions are unchanged.
The RGB transmission also feeds the existing NRD diffuse fractions.
The private material StructuredBuffer grows 112 -> 128 bytes: thickness at
112, RGB sigmaA at 116. Root constants remain 96 bytes / 24 DWORDs, and the
root signature, shared CBs, and raster GBuffer do not grow.
CPU upload/stride/hash consumer:
`pipeline_path_tracer_pass.{h,cpp}`. GPU declaration:
`pathtracer_shared.hlsli`; shading consumer: `layered_bsdf.hlsli` through
`pathtracer_closesthit.hlsl` (including NRD) and `hair_bcsdf.hlsli` surface dispatch.
Raygen/miss and ReSTIR initial/temporal/spatial shaders include the shared
declaration; the Tier C `MaterialData` entry in `test_cbuffer_layouts.py`
reflects every member and the stride.
Existing `test_pipeline_path_tracer.cpp` and `test_restir_di.cpp` layout
assertions are also consumers and now expect 128 bytes; the former checks the
new offsets and zero-absorption defaults.

- [x] DXR.S2.1 Absorption/tint matches the analytic Snell/Beer-Lambert formula.
  RTX 5080 PNGs in `E:/UzmiEngine/layered-stage2/{base,clear,tint,thick}/frames/f_31.png`
  were read visually: white/gray uncoated and clear targets become red-brown with
  absorption, then darker/redder at twice thickness. Central 8x8 patch RGB codes:
  base (112,112,112), clear (103,103,103), tint (93,49,23), thick (83,23,5).
  Known configuration: NoV approximately 1, NoL=cos(45 degrees), IOR 1.5,
  thickness 1 cm, sigmaA=(0.1,0.7,1.4)/cm; Snell path=1.066946709514 cm.
  Expected RGB attenuation=(0.898800061,0.473850837,0.224534616);
  measured=(0.902897860,0.475648443,0.223182878), maximum absolute error
  0.004097799 (gate 0.015 for 8-bit quantization). At thickness 2 cm:
  expected=(0.807841549,0.224534616,0.050415794),
  measured=(0.805795720,0.223182878,0.048399026), max error 0.002045830.
  Measurement subtracts the analytic unabsorbed white coat reflection
  (0.015658193 RGB codes), using tonemap-0 linear passthrough and a collimated
  source; absorption acts only on the transmitted base, not the white reflection.
  The shared CPU test independently checks Snell using asin/cos in double
  precision; at thickness 2, NoV 1, NoL 0.5, IOR 1.5 the measured float path
  is 2.224744797 and RGB attenuation is (0.800535440,0.210699722,0.044394370).
- [x] DXR.S2.2 Zero absorption reproduces committed stage-1 GPU output exactly.
  `layered-stage2/stage1/frames/f_255.png` versus
  `layered-stage1/coat/frames/f_255.png`: maximum RGB code delta 0, mean absolute
  delta 0. The shared CPU regression also checks bit-exact stage-1 composition
  at zero sigmaA across thicknesses, view cosines, IORs, and base values.
- [x] DXR.S2.3 Relative furnace energy: absorbing coat <= clear coat <= base + F
  under the same numerical gate as stage 1 (not an absolute-energy claim).
  72 deterministic furnace cases, 256x512 cosine-hemisphere quadrature points
  each, NoV {1,0.5,0.1,0.01}, base roughness {0.3,0.8,1}, coat roughness
  {0.25,0.5,1}, metallic {0,1}, IOR 1.5, sigmaA 0.7/cm, thickness 2 cm.
  Maximum absorbed-base=-0.208294806; maximum absorbed-base-F=-0.248294809;
  maximum absorbed-clear=-0.026566243. Every quadrature point checks absorbed
  <= clear; separate boundary cases check finite attenuation in [0,1] and
  monotonicity with thickness. Unchanged stage-1 furnace also passes:
  max clear-base-F=-0.010773443, inherited maximum clear albedo=1.142395244.
  Ten focused tests pass (6 existing stage 1 + 4 new stage 2), exit 0:
  `BeerLambertCoatPathViaSnell`, `ZeroAbsorptionIsBitExactStage1`,
  `AbsorptionOnlyRemovesTransmittedEnergy`,
  `AbsorbingWhiteFurnaceRelativeToUnchangedBase` in suite `LayeredBSDFStage2`.
  Evidence: `layered-stage2/coat-tests.{log,xml}` and `metrics.json`.
- [x] DXR.S2.4 Build, layouts, default byte match, and full gtest regression.
  Final release build exit 0, 152 embedded shaders verified; layout suite exit 0,
  Tier A 89/89, Tier B 33/33, Tier C 12/12, material stride 128 bytes.
  All six final captures exit 0 and all six run.log files contain no
  `compile failed`; logs confirm RT-PSO creation and layered DispatchRays.
  Default raster `layered-stage2/default/frames/f_95.png` versus
  `f005-after/frames/f_95.png`: maximum RGB code delta 0.
  Full `GTEST_FILTER='-RuntimeViewportTest.*' zig build test
  -Doptimize=ReleaseFast`: 7882 passed (+4 vs the 7878 baseline), 17 skipped,
  and only the same 3 pre-existing VfxEditorSlate failures:
  WorkbenchHasTwoColumns, TitleBarReflectsEmitterName,
  StatusBarBindingsReflectTelemetry. Exit 1 is solely those existing failures;
  no TDR/device removal or new regression. Final evidence:
  `E:/UzmiEngine/layered-stage2/gtests-final.{log,xml}` (42.938 seconds of tests).
  Two stale 112-byte material assertions found by the initial full run were
  updated to the verified 128-byte contract before the final run.
  Process inspection after completion found no engine, test_runtime, or zig
  processes. No commit or archive was created.
  Requested final recheck also passed the build (exit 0, 152 shaders) and
  `npm run test:layouts` (exit 0, A 89/89, B 33/33, C 12/12).
  The first recheck process aborted during the existing
  `WorldTest.StaleHandle_GetComponent_DeathTest` without a device-removal marker.
  Rerunning with the prior successful launch setup completed all 7902 tests in
  44.146 seconds: 7882 passed, 17 skipped, exactly the same 3 VfxEditorSlate
  failures, exit 1. Fresh evidence under `E:/UzmiEngine/layered-stage2/`:
  `recheck-build.log`, `recheck-layouts.log`,
  `recheck-gtests-retry.{log,xml}`; the aborted attempt is retained separately as
  `recheck-gtests.log`. Final cleanup confirmed `NO_ENGINE_TEST_ZIG_PROCESSES`.
- [ ] DXR.S2.DEFERRED Physically refracted adjacent-IOR N-layer stack. Deferred to
  a dedicated stage with directional Jacobians, internal exit/TIR treatment, and
  an explicit stage-1 compatibility decision; no independent air-referenced
  GGX lobes are claimed as a physically correct stack here.

Still deferred: the original deferred-renderer sessions, per-material authoring,
layered ReSTIR targets, thin-film interference, anisotropic coats, and full spectral
transport.

Reproduce each RTX run separately with
`python scripts/rendering/test_layered_bsdf_stage2.py --out E:/UzmiEngine/layered-stage2 --case CASE`,
where CASE is base/clear/tint/thick/stage1/default/metrics. Each render has a
180-second process timeout; the metrics case re-reads all PNGs and asserts the
tint tolerance and both byte identities.

Stage-2 changed files (all paths relative to the repository; no commit made):
- `.rulebook/tasks/phase1_falcor-layered-materials/tasks.md`
- `runtime/engine/renderer/passes/pathtracing/pipeline_path_tracer_pass.cpp`
- `runtime/engine/renderer/passes/pathtracing/pipeline_path_tracer_pass.h`
- `runtime/shaders/runtime/pathtracing/layered_bsdf.hlsli`
- `runtime/shaders/runtime/pathtracing/pathtracer_shared.hlsli`
- `runtime/tests/engine/renderer/test_layered_bsdf_stage2.cpp`
- `runtime/tests/engine/renderer/test_pipeline_path_tracer.cpp`
- `runtime/tests/engine/renderer/test_restir_di.cpp`
- `scripts/rendering/layered-absorption-stage2.uzproject`
- `scripts/rendering/layered-absorption-stage2.uzscene`
- `scripts/rendering/test_cbuffer_layouts.py`
- `scripts/rendering/test_layered_bsdf_stage2.py`
