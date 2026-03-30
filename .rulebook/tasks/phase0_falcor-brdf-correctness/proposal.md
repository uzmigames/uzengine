# Proposal: phase0_falcor-brdf-correctness

## Why
Falcor comparison found 2 correctness bugs in existing BRDFs and 8 missing BSDF implementations:
1. **`D_GGX` has no alpha=0 guard** — diverges to infinity for perfect mirror surfaces (alpha < 1e-3)
2. **`Fd_Burley` missing `/1.51` energy conservation** — rough diffuse surfaces are ~51% over-bright at max roughness (@ref Lagarde 2015)
3. **Only 1 material type** (metallic-roughness PBR) — Falcor has 10+ (cloth, hair, glass, physical metals, layered)
4. **Missing Fresnel models** — no conductor Fresnel for real metals (gold, copper), no dielectric Fresnel for glass/water
5. **Missing BSDFs** — no Oren-Nayar (rough diffuse), no anisotropic GGX (brushed metal)
6. **NDF sampling only** — VNDF sampling (Heitz 2018) gives 2-4× lower variance for IBL and path tracing
7. **No multi-scatter energy compensation** — rough metallic surfaces appear too dark (Turquin 2019)

## What Changes
- Fix `D_GGX` alpha guard and `Fd_Burley` energy conservation in `pbr_common.hlsli`
- Unify Smith visibility to single `V_SmithGGXCorrelated` (remove `Vis_SmithJointApprox` duplicate)
- Implement `F_FresnelDielectric`, `F_FresnelConductor`, `F_SchlickGeneralized`, `ReflectanceToIOR`
- Implement `Fd_OrenNayar` diffuse BRDF for rough matte surfaces
- Implement `ImportanceSampleGGX_VNDF` for lower-variance sampling
- Implement `D_GGX_Anisotropic` + `V_SmithGGXCorrelated_Anisotropic`
- Add multi-scatter energy compensation to BRDF LUT (`brdf_lut_cs.hlsl`)
- Add mip-bias firefly suppression to `prefilter_cs.hlsl`
- Add `materialType` field to GBuffer for multi-BSDF dispatch
- Add tangent direction to GBuffer for anisotropic materials

## Impact
- Affected specs: `docs/specs/rendering/ibl-pipeline.md`, `docs/specs/rendering/shader-library.md`
- Affected code: `pbr_common.hlsli`, `deferred_lighting_ps.hlsl`, `brdf_lut_cs.hlsl`, `prefilter_cs.hlsl`, `gbuffer_ps.hlsl`
- Breaking change: YES (Fd_Burley energy fix changes diffuse brightness by ~34% at roughness=1.0 — intentional correctness fix)
- User benefit: Physically accurate metal/glass materials, correct diffuse brightness, cloth and brushed metal shading models, higher quality IBL reflections
