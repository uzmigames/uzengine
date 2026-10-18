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

Deferred: the deferred-renderer GBuffer/material dispatch, Kulla-Conty fitted
compensation, per-material authoring, coat tint/absorption, layered ReSTIR targets,
full multilayer stacks, thin-film iridescence, and anisotropic coats.
