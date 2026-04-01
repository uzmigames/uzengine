## BRDF Correctness + New Material BSDFs (from Falcor analysis)
> Source: `docs/analysis/falcor/unified-shader-improvements.md` + `shaders-and-materials.md`
> Estimate: 4 sessions

## Session 1: Fix existing BRDF bugs

- [ ] 1.1 `include/pbr_common.hlsli` — `D_GGX`: add alpha < 1e-3 guard (return 0 for delta BSDF / perfect mirror). @ref Falcor `SpecularMicrofacet.slang`.
- [ ] 1.2 `include/pbr_common.hlsli` — `Fd_Burley`: add Frostbite energy conservation factor `energyFactor = lerp(1.0, 1.0/1.51, roughness)`. @ref Lagarde 2015.
- [ ] 1.3 `deferred_lighting_ps.hlsl` — replace `Vis_SmithJointApprox` with `V_SmithGGXCorrelated` from `pbr_common.hlsli`. Unify to single implementation.
- [ ] 1.4 Build + capture screenshots. Compare before/after — rough diffuse should be ~34% darker at roughness=1.0. Verify metallic surfaces unchanged.

## Session 2: Conductor + Dielectric Fresnel

- [ ] 2.1 `include/pbr_common.hlsli` — implement `F_FresnelDielectric(float eta, float cosThetaI)` → `float`. Full Snell's law, Rs + Rp average. @ref PBR Book 2018 ch.8.
- [ ] 2.2 `include/pbr_common.hlsli` — implement `F_FresnelConductor(float3 eta, float3 k, float cosThetaI)` → `float3`. Complex Fresnel with extinction coefficient. @ref Shirley 1997.
- [ ] 2.3 `include/pbr_common.hlsli` — implement `ReflectanceToIOR(float reflectance)` → `float`. Convert F0 to IOR: `(1+sqrt(r))/(1-sqrt(r))`.
- [ ] 2.4 `include/pbr_common.hlsli` — implement `F_SchlickGeneralized(float cosTheta, float f0, float f90, float exponent)` — tunable power Fresnel. @ref Falcor variant.
- [ ] 2.5 Add conductor Fresnel LUT data for common metals: gold (η=0.18,k=3.42), copper (η=0.27,k=3.35), aluminum (η=1.66,k=9.22), iron (η=2.95,k=3.01), silver (η=0.16,k=3.80). Store as constants in `pbr_common.hlsli`.
- [ ] 2.6 Build + verify: Schlick F0=0.04 dielectric should match `F_FresnelDielectric(1.5)` within 1%.

## Session 3: Oren-Nayar + VNDF sampling + multi-scatter compensation

- [ ] 3.1 `include/pbr_common.hlsli` — implement `Fd_OrenNayar(float3 albedo, float roughness, float NdotV, float NdotL, float VdotL)`. @ref Oren & Nayar 1994. Full A+B formula.
- [ ] 3.2 `include/pbr_common.hlsli` — implement `ImportanceSampleGGX_VNDF(float2 u, float3 V, float alpha)` → `float3`. @ref Heitz 2018 "Sampling the GGX Distribution of Visible Normals".
- [ ] 3.3 `ibl/brdf_lut_cs.hlsl` — add multi-scatter energy compensation channel. Compute `Ems = 1 - Ess` using Turquin 2019 fit. Store in B channel or separate LUT. @ref Turquin 2019.
- [ ] 3.4 `ibl/brdf_lut_cs.hlsl` — switch from NDF to VNDF sampling for lower variance at grazing angles. @ref Heitz 2018.
- [ ] 3.5 `ibl/prefilter_cs.hlsl` — add mip-bias for firefly suppression on rough reflections. @ref Lagarde 2014.
- [ ] 3.6 Build + verify: rough metals should appear brighter (energy compensation). IBL quality should improve.

## Session 4: Anisotropic GGX + material type dispatch

- [ ] 4.1 `include/pbr_common.hlsli` — implement `D_GGX_Anisotropic(float NdotH, float TdotH, float BdotH, float alphaT, float alphaB)`. @ref Walter 2007 anisotropic extension.
- [ ] 4.2 `include/pbr_common.hlsli` — implement `V_SmithGGXCorrelated_Anisotropic(float alphaT, float alphaB, float TdotV, float BdotV, float TdotL, float BdotL, float NdotV, float NdotL)`.
- [ ] 4.3 Add `materialType` uint field to GBuffer (packed in unused alpha or separate RT). Values: 0=Standard, 1=Cloth, 2=Subsurface, 3=ClearCoat.
- [ ] 4.4 `deferred_lighting_ps.hlsl` — add material type switch: Standard uses GGX+Burley, Cloth uses Charlie+Neubelt sheen (from `sheen.hlsli`).
- [ ] 4.5 `geometry/gbuffer_ps.hlsl` — write tangent direction to GBuffer for anisotropic materials (R10G10B10A2 packed tangent).
- [ ] 4.6 Build + verify: brushed metal (anisotropic) and cloth (sheen) render correctly with distinct shading models.
