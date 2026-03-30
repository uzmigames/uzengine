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
