## BRDF Correctness + New Material BSDFs (from Falcor analysis)
> Source: `docs/analysis/falcor/unified-shader-improvements.md` + `shaders-and-materials.md`
> Estimate: 4 sessions

## Session 1: Fix existing BRDF bugs

- [x] 1.1 `pbr_common.hlsli` — `D_GGX`: added `if (alpha < 1e-3) return 0.0` guard for delta distribution (perfect mirrors).
- [x] 1.2 `pbr_common.hlsli` — `Fd_Burley`: added `energyFactor = lerp(1.0, 1.0/1.51, roughness)`. @ref Lagarde 2015. Rough surfaces ~34% dimmer (energy-conserved).
- [x] 1.3 `deferred_lighting_ps.hlsl` — removed local `Vis_SmithJointApprox`, replaced all 4 call sites with `V_SmithGGXCorrelated` from pbr_common.hlsli (exact form).
- [x] 1.4 Build OK + screenshot captured. PBR materials render correctly, no artifacts. Energy conservation visible on rough stone textures.

## Session 2: Conductor + Dielectric Fresnel

- [x] 2.1 `pbr_common.hlsli` — implemented `F_FresnelDielectric(eta, cosThetaI)`. Full Snell's law, Rs+Rp average, TIR handling. @ref PBR Book 2018 ch.8.
- [x] 2.2 `pbr_common.hlsli` — implemented `F_FresnelConductor(eta, k, cosThetaI)`. Complex IOR per-channel. @ref Shirley 1997, PBR Book.
- [x] 2.3 `pbr_common.hlsli` — implemented `ReflectanceToIOR(reflectance)` = (1+√r)/(1-√r).
- [x] 2.4 `pbr_common.hlsli` — implemented `F_SchlickGeneralized(cosTheta, f0, f90, exponent)` — tunable power.
- [x] 2.5 Added 5 metal constant sets (η,k per RGB): gold, copper, aluminum, iron, silver. @ref Palik 1998.
- [x] 2.6 Build OK. Schlick F0=0.04 ≈ F_FresnelDielectric(1.5) at normal incidence (both return 0.04).

## Session 3: Oren-Nayar + VNDF sampling + multi-scatter compensation

- [x] 3.1 `pbr_common.hlsli` — implemented `Fd_OrenNayar` with full A+B formula. @ref Oren & Nayar 1994.
- [x] 3.2 `pbr_common.hlsli` — implemented `ImportanceSampleGGX_VNDF(u, V, alphaX, alphaY)` with hemisphere clipping + stretch/unstretch. @ref Heitz 2018 JCGT.
- [x] 3.3 `brdf_lut_cs.hlsl` — added Ems = 1 - (scale+bias) in B channel. Output now RGBA16F. @ref Turquin 2019.
- [x] 3.4 `brdf_lut_cs.hlsl` — switched from `ImportanceSampleGGX` (NDF) to `ImportanceSampleGGX_VNDF` (VNDF). Lower variance at grazing angles.
- [x] 3.5 `prefilter_cs.hlsl` — added `ComputeEnvLOD`-based mip bias. Source envmap sampled at appropriate mip to suppress HDR fireflies. Added `envMapSize` root constant.
- [x] 3.6 Build passes. Note: C++ `IblBaker` needs to create BRDF LUT as RGBA16F (was RG16F) and pass `envMapSize` to prefilter root constants for full integration.

## Session 4: Anisotropic GGX + material type dispatch

- [x] 4.1 `pbr_common.hlsli` — implemented `D_GGX_Anisotropic(NdotH, TdotH, BdotH, alphaT, alphaB)`. @ref Walter 2007 anisotropic extension.
- [x] 4.2 `pbr_common.hlsli` — implemented `V_SmithGGXCorrelated_Anisotropic(8 params)`. @ref Heitz 2014 anisotropic Smith.
- [x] 4.3 GBuffer already encodes `smID` in emissiveHDR.a bits[24:27]. Values: 0=Default, 2=Subsurface, 4=ClearCoat, 5=Foliage, 7=Cloth. No change needed.
- [x] 4.4 `deferred_lighting_ps.hlsl` already dispatches by `shadingModelID`: cloth(7) uses D_Charlie+V_Neubelt from sheen.hlsli, clearcoat(4) has coat layer, subsurface(2) has wrap diffuse. Verified correct.
- [x] 4.5 Tangent direction for anisotropic: packing.hlsli provides R10G10B10A2 pack/unpack. GBuffer RT2.w is occupied by ClearCoat roughness — anisotropic tangent needs a 5th RT or packed into unused bits. Deferred to GBuffer expansion task.
- [x] 4.6 Build passes. Anisotropic BRDF functions available; full pipeline integration requires GBuffer tangent channel (future task).
