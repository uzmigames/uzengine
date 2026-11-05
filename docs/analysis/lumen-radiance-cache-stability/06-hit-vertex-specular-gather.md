# 06 - Hit-vertex sun specular for the GI gather: implementation spec

Date 2026-11-02. UzEngine, branch feat/soft-shadows-area-lights. Design spec only (no code in this
lane); every claim below is cited to the file and line it was read from so the implementation is one
pass. Companion evidence: `E:/UzmiEngine/_evidence/gi/indirect-underlit/report.md` (the measured
decomposition; its section 5 is the fix sketch this spec turns into a contract).

## 0. Decision in five lines

1. The real-time indirect is 0.40-0.59x of the DXR path tracer's indirect because the gather shades
   every secondary hit with Lambert only (report.md:20-24, :153-161). A diffuse-only-NEE PT control
   dropped the PT indirect to 0.80x, so the hit vertices' GGX reflection of the sun is 20.0% of the
   PT indirect = 33.5% of the whole deficit (report.md:123). That is the lever this spec closes.
2. Add, at the gather's single hit-shading point and in the radiance-cache probe writer, the sun's
   specular lobe `sunColor * visibility * D * Vis * F * NoL` with V = -rayDir, L = sunDir, N = the hit
   normal, and weight the existing sun Lambert by `(1 - E_spec(NoV, roughness))` from the IBL BRDF LUT
   exactly as the D7 path-tracer fix does (`PathDiffuseEnergy`), so the hit's directional albedo stays
   <= 1 (section 4).
3. Roughness is a scene constant (CVar, default 0.7) and F0 = 0.04: neither the GI material table nor
   the SDF clipmap carries roughness or metallic (section 2.4). Per-mesh roughness is a follow-up.
4. Gate: `r.GI.FinalGather.HitSpecular` (bool, default 0) selects compile-time permutations
   (`UZ_GI_HIT_SPECULAR=1`) of the six affected compute pipelines; the default pipelines compile the
   unchanged text, so OFF is byte-identical by construction (the UZ_RC_STABILITY / UZ_GATHER_DXR
   precedent, section 6). Knobs travel as root DWORDs declared only inside the permutation.
5. Acceptance: the sky-off, feed-removed exact gather (`rt_dxr_k0_nosky`) rises from 0.48x of the PT's
   sun-only first bounce toward 0.61x (the PT's own specular share bounds the gain at x1.27), floor
   contacts do not exceed the PT with the feed removed, per-ray luminance tails do not grow more than
   1.5x, OFF runs reproduce the archived results to 5 decimals, build/layout gates green (section 9).

## 1. Evidence this spec rests on

- Deficit and ranking: report.md:10-29 (summary), :116-127 (transport waterfall: (a) hit specular
  33.5%, (b) first-bounce diffuse shortfall 33.0%, (c) feed shortfall 20.5%).
- The control that proves the mechanism: temporary instrumentation at pathtracer_closesthit.hlsl:305
  (light-BVH NEE) and :364-365 (directional NEE) made the NEE BRDF diffuse-only at vertices >= 1;
  reverted, `git diff` empty (report.md:123, :260-262). Those two lines are where the PT evaluates
  `EvaluatePathSurface(N, V, L, albedo, roughness, metallic, ...)` = the full GGX + D7-coupled Lambert
  (pathtracer_shared.hlsli:144-176).
- Why "uniform across ROIs": every receiver sees sun-lit floors and walls at grazing angles where
  Fresnel is 25-45% of the energy (report.md:155-161); per-ROI diffuse-only/full = 0.66-0.92.
- D7 = the path tracer's diffuse/specular energy coupling, commit 7f0a7e3f: Lambert is weighted by
  the complement of the specular directional albedo `E_spec = F0*A + B` from the IBL LUT, with a 0.005
  reserve and a linear horizon extension below the LUT's NoV >= 0.001 domain
  (docs/analysis/path-tracing/10-d7-diffuse-specular-energy.md:9-17, :48-53;
  docs/analysis/volumetric-clouds-rewrite/decisions-pending-2026-10-16.md:82-83). D7 is locked: the
  base BRDF is not touched (docs/specs/rendering/rt-reflections.md:95).
- Report section 5 (report.md:153-187) is the sketch: term, insertion sites, LUT coupling, alpha
  clamp, compile-time permutation recommendation, validation runs. This spec confirms each item
  against the source and settles what the sketch left open (roughness source, the cell paths, the
  consumer set, the root-constant plumbing, the probe writer, the firefly bound).

## 2. The hit as it is shaded today (what is reachable at the hit)

### 2.1 Consumers of the hit shading

`ShadeSdfReflectionHit` is "the single hit-shading point of every SDF consumer (the final gather,
ReSTIR GI, the deferred SDF reflections)" (sdf_reflections.hlsli:216-218). Include graph:

| shader | path to the hit shading | pipeline |
|---|---|---|
| ssgi/ssgi_trace_cs.hlsl | :41 ssgi_world_gather.hlsli -> :5 gi/sdf_diffuse_gather.hlsli -> :6 sdf_reflections.hlsli, :7-9 dxr_diffuse_gather.hlsli under UZ_GATHER_DXR; the gather loop calls `TraceDxrGatherRadiance` or `TraceSdfSurfaceRadianceIdentified` (sdf_diffuse_gather.hlsli:102-106) | SSGI_Trace / SSGI_DXRTrace (pipeline_gi.cpp:231-243) |
| ssgi/ssgi_upsample_cs.hlsl | :20 ssgi_world_gather.hlsli; full-res fallback gather `GatherSurfaceLighting` (:105) | SSGI bilateral upsample / SSGI_DXRUpsample (pipeline_gi.cpp:245-257) |
| ssgi/ssgi_restir_{initial,spatial,temporal}_cs.hlsl | :10/:15/:14 ssgi_restir_gi.hlsli -> :48 ssgi_world_gather.hlsli; traces at ssgi_restir_initial_cs.hlsl:61 and ssgi_restir_gi.hlsli:154 | ReSTIR GI pipelines (pipeline_gi.cpp:463-464) - engine default OFF (report.md:46-47) |
| lighting/deferred_lighting_ps.hlsl | :220 gi/sdf_diffuse_gather.hlsli; builds its own `SdfReflectionScene` (:1404-1423) for `GatherSdfReflections` (:1438) = the SDF REFLECTIONS consumer, not the diffuse gather | DeferredLightingPS |
| gi/radiance_cache_trace_cs.hlsl | own inline hit shading (:710-765), the same Lambert form | RadianceCache_Trace / _Stability (pipeline_gi.cpp:663-686) |
| gi/surface_cache_{feed,relight,indirect}_cs.hlsl | surface_cache_hit.hlsli (`SurfaceCacheDirectLayer` :122-128) - writes the cells' `sun` layer | surface cache writers - engine default OFF |
| gi/ddgi_probe_trace_cs.hlsl | own hit shading, `sunTerm = rt.rgb; // albedo * sunRad / pi` (:139) | DDGI writer - separate cache, out of scope (section 10) |
| reflections/rt_reflection_cs.hlsl | `ReflectionHitRadiance` = Lambert sun + probe term (rt_reflection_contract.hlsli:181-187) | RT reflections - out of scope, kept Lambert (section 8.3) |

### 2.2 Site A - SDF path: `ShadeSdfReflectionHit` (sdf_reflections.hlsli:229-269)

- Inputs: `scene.sunDirection`, `scene.sunColor` (:44-45; set from PerFrameCB `lightDir` / `lightColor`
  = colour x intensity at ssgi_world_gather.hlsli:26-27 and deferred_lighting_ps.hlsl:1417-1418).
- N = `SdfReflectionNormal(scene, position)` (:230), the field gradient, unit (:154-162); back faces
  rejected at :231 so `dot(N, -rayDirection) > 0` is guaranteed afterwards.
- V = `-rayDirection` (the parameter at :229).
- L = `scene.sunDirection`; `ndl` at :250; sun visibility = a field shadow trace at :252-254 (ray kind
  2), `visibility` in {0, 1}.
- albedo = `saturate(SampleSdfReflectionSurface(scene, position).gba)` (:255): the clipmap's per-mesh
  average diffuse reflectance, already multiplied by (1 - metallic) at bake time
  (global_sdf_composite_cs.hlsl:56-62). No roughness, no metallic, no mesh id at the hit.
- Outgoing radiance, three return sites that carry the sun term:
  - :249 `return cellSun + cellOther;` - surface-cache cell replaces the WHOLE hit radiance (D7 reader
    policy, surface_cache_contract.hlsli:393). No inline sun here.
  - :256-259 DirectMode 1: `albedo * (scene.sunColor * (ndl * visibility * M_1_PI)) + cellOther`.
  - :260-266 SdfWrites diagnostic: `sunTerm` (Lambert) and `otherTerm` written to the cell, returned.
  - :267-268 inline (the shipping path): `albedo * (scene.sunColor * (ndl * visibility * M_1_PI) +
    SdfReflectionIrradiance(scene, origin, normal, hitT))`.
- Already included: `../lighting/ibl.hlsli` (:7) which includes `../material/brdf.hlsli` (ibl.hlsli:9):
  `D_GGX` (brdf.hlsli:24-29), `V_SmithGGXCorrelated` (:94-100), `F_Schlick(float3 f0, float cos)`
  (:131-134), `ComputeF0` (:189-192), `RoughnessToAlpha` (:198-201, floor `kMinGGXAlpha` = 0.0064,
  constants.hlsli:52) and `EnvironmentBRDFApprox` (ibl.hlsli:19-25) are all in scope at this site.
- Sampler for a LUT read: `UZ_SDF_REFLECTION_SAMPLER` = linear clamp at s2 (:11-14).

### 2.3 Site B - DXR path: `TraceDxrGatherRadiance` (dxr_diffuse_gather.hlsli:32-92)

- N: interpolated vertex normal from the material table transformed by WorldToObject, normalized and
  flipped against the ray (:62-74); the SDF gradient where the mesh has no attributes (:67-68) and in
  the mode-2 control (:37-46).
- V = `-direction`; L = `scene.sunDirection`; `ndl` at :87; `visibility` = `GatherDxrSunVisibility`
  (TLAS shadow ray, :18-30) at :88-89.
- albedo = triangle-UV textured albedo from `GITrace_HitSurface` (gi_hit_material.hlsli:42-90);
  `emission` from `GITrace_HitEmissive` (:75-76).
- Return at :90-91: `emission + albedo * (scene.sunColor * (ndl * visibility * M_1_PI) +
  SdfReflectionIrradiance(scene, position, normal, hitT))`.
- The DXR header is compiled only under `UZ_GATHER_DXR` (dxr_diffuse_gather.hlsli:3-4,
  sdf_diffuse_gather.hlsli:7-9).

### 2.4 Site C - probe writer: radiance_cache_trace_cs.hlsl main loop (:668-802)

- N = `GdfNormal(hitPosition)` (:712); the ray direction `dir` plays the view: V = `-dir`.
- L, NdL, visibility (field shadow trace from a 2*kSurfaceEpsilon lifted origin) at :737-743;
  `sunRad = lightColor * lightIntensity * NdL * visibility` (:744-745) - the b2 trace constants
  (:92-99; CPU mirror radiance_cache.h:470-478).
- albedo = `saturate(SampleGdfSurface(hitPosition).gba)` (:750); `indirect` = previous-frame probe
  irradiance (:751-755).
- `radiance = dot(N, -dir) > 0.0 ? albedo * (sunRad / kPI + indirect) : 0.0` (:756); stats :757-761;
  DirectMode 1 override `albedo * (sunRad / kPI) + cellOther` (:763); a readable cell replaces the
  radiance at :729-735 (no inline sun).
- Already included: `../lighting/ibl.hlsli` (:17) -> brdf.hlsli; sampler `linearClamp` at s2 (:19).
  This shader binds root constants (b0, :23-57) and the trace CB (b2, :61-137) and NOT PerFrameCB, so
  the LUT SRV must be passed to it (section 6.4).

### 2.5 What the hit does NOT have

- Roughness / metallic: `GIMeshMaterial` carries tint, albedo SRV, geometry views, emission and one
  `padding` DWORD (gi_hit_material.hlsli:24-37); the composite stores `DiffuseReflectance` only
  (global_sdf_composite_cs.hlsl:56-62). The PT's `MaterialData` does carry `roughnessSrv`,
  `metallicSrv`, multipliers and flags (pathtracer_shared.hlsli:87-91; fetched at
  pathtracer_closesthit.hlsl:85-102) - a different table the gather does not bind.
- The BRDF LUT at the probe writer (no PerFrameCB; see 2.4).
- Any view direction other than the gather ray's: the hit's outgoing radiance toward the receiver is
  what a gather ray returns, so V = -rayDir is exactly right for a directional lobe.

### 2.6 How the engine already couples diffuse and specular (what "mirroring D7" means)

- PT (the reference, pathtracer_shared.hlsli:163-174): sample the LUT at `clamp(float2(NoV,
  roughness), halfTexel, 1 - halfTexel)`, `specularAlbedo = f0 * ab.x + ab.y`, per-channel
  `diffuseEnergy = PathDiffuseEnergy(specularAlbedo, NoV)` (pathtracer_math.hlsli:40-50:
  `max(0, 1 - E_spec - 0.005) * grazing`, grazing = NoV / 0.001 below the LUT domain), then
  `(diffuseEnergy * (1 - metallic) * albedo / PI + specular) * NoL`.
- LUT layout: U = NdotV, V = roughness, R = scale (F0 multiplier), G = bias (brdf_lut_cs.hlsl:5-8),
  1024 VNDF samples (:47-75), NdotV >= 0.001 (:86). Bound in PerFrameCB as `brdfLutSrvIndex`
  (constant_buffers.h:65; HLSL mirrors deferred_lighting_ps.hlsl:83, pathtracer_shared.hlsli:78,
  translucent_ps.hlsl:46 with `packoffset(c14.x)`).
- D7 explicitly rejected the Karis analytic fit for the PT: independent quadrature found about 0.08
  under-estimation at some grazing configurations (10-d7-diffuse-specular-energy.md:22-23). The
  deferred raster path uses the analytic `EnvironmentBRDFApprox` for its ambient split-sum and
  multi-scatter (deferred_lighting_ps.hlsl:1459, :1470-1522) and the Frostbite diffuse for the sun
  (:878-879); neither is touched here.
- Raster sun specular, for the convention: `spec = D_GGX(alpha, NoH) * V_SmithGGXCorrelated(alpha,
  NoV, NoL) * F_Schlick(F0, F90, VoH)`, `Lo = (diff + spec) * lightColor.xyz * NoL * shadow`
  (deferred_lighting_ps.hlsl:866-881). The PT's primary vertex does the same (path_tracer_ps.hlsl:
  364-385). The hit term below is this expression at the hit with `lightColor` = `scene.sunColor`.

## 3. Designs considered

### Design A (recommended) - directional sun lobe at the hit, compile-time permutation

The term of section 4 at sites A, B, C; `(1 - E_spec)` on the sun Lambert from the LUT (D7 verbatim);
scene-constant roughness, F0 = 0.04; `UZ_GI_HIT_SPECULAR=1` permutations of the gather trace/upsample
(x plain/DXR) and the RC trace (x plain/stability) = 6 lazily-built pipelines; knobs in root DWORDs
declared only under the define; the deferred PS and the reflection passes never define it.

- Pros: closes the measured mechanism (the directional grazing-Fresnel glint the PT control isolated);
  deterministic per ray (no new random dimension); OFF is byte-identical by construction (the
  precedent of UZ_RC_STABILITY: "the default pipeline compiles exactly the text before them",
  radiance_cache_trace_cs.hlsl:123-127; UZ_GATHER_DXR: pipeline_gi.cpp:235-236); one GGX evaluation
  and one LUT tap per hit; all helpers exist at both sites.
- Cons: 6 more pipelines (lazily created only when the CVar is on); pipeline-selection and
  root-count logic in three places; the surface-cache cell paths and ReSTIR GI are not covered in
  pass one (sections 8.2, 8.6); roughness is a scene constant until the material table carries it.

### Design B - runtime flag in `SdfReflectionScene`, one pipeline each

Add `hitSpecularRoughness` to the scene struct and a uniform branch `if (roughness > 0)`.

- Pros: no permutation matrix; one code path.
- Cons: the default DXIL changes. The OFF value is arithmetically identical only if the compiler keeps
  the existing expression's operation order and FMA contraction; the driver compiler may re-fuse
  `mul`/`add` differently once the surrounding code changes, so OFF cannot be asserted bit-identical,
  only measured (report.md:173-176 flags exactly this). The repository's precedent for "bit-exact when
  off" is the define route. Rejected.

### Design C - Lumen's view-independent fold (fully-rough specular into the diffuse albedo)

Lumen's surface-cache path shades hits with `Diffuse_Lambert(ApproxFullyRoughDiffuseColor)` where
`ApproxFullyRoughDiffuseColor = DiffuseColor + ApproxFullyRoughSpecularColor` and the latter is
`EnvBRDFApproxFullyRough(SpecularColor)` ("Return approximate diffuse color as if the entire surface
was diffuse in order to conserve energy in secondary bounces", E:/UnrealGITHUB/Engine/Shaders/Private/
Lumen/LumenHardwareRayTracingCommon.ush:775-778, :864, :867, :945, :950-953). Only Lumen's opt-in hit
lighting evaluates direct lighting with the real BRDF at the hit (`bHitLightingDirectLighting`,
:171, :885-888).

- Pros: view-independent, so it is cache-compatible (surface-cache cells, DDGI atlas, even the SDF
  composite's reflectance channel); trivially cheap; no variance.
- Cons: it folds the HEMISPHERICAL average of the lobe, about 0.45 * F0 = 0.018 of albedo for a
  dielectric, i.e. a 2-4% lift of a 0.5-albedo Lambert - not the 21% of the first bounce the PT
  control measured, which is the directional sun glint at grazing Fresnel. It does not move the
  metric. Kept as the right tool for the cells' view-independent `sun` layer later (section 10).

### Recommendation

Design A. It is the only one that reproduces the measured mechanism, and its byte-identical OFF is
structural. Boundaries it assumes: the gather's hit radiance is "radiance leaving the hit toward the
ray origin" (true at all three sites), the sun is the only NEE light at hits (true today: local lights
reach hits only through the surface cache's `sun` layer, surface_cache_hit.hlsli:125-126), and the
deferred PS keeps the reflections consumer unchanged.

## 4. The term and the energy coupling

At a hit with unit normal N, V = -rayDir (dot(N, V) > 0 guaranteed at all three sites), L = sun
direction, `E_sun` = `scene.sunColor` (site A/B) or `lightColor * lightIntensity` (site C) and the
already-computed `visibility` and `NoL` (`ndl` / `NdL`):

```
H     = normalize(V + L);  NoV = saturate(dot(N, V));  NoH = saturate(dot(N, H));  VoH = saturate(dot(V, H))
alpha = RoughnessToAlpha(roughness)                                  // brdf.hlsli:198-201
dv    = min(D_GGX(alpha, NoH) * V_SmithGGXCorrelated(alpha, NoV, NoL) * NoL, kGiHitSpecularMaxBrdfCos)
specularCos   = dv * F_Schlick(F0, VoH)                              // f_spec * cos, float3
ab            = LUT.SampleLevel(clampSampler, clamp(float2(NoV, roughness), halfTexel, 1 - halfTexel), 0).rg
E_spec        = F0 * ab.x + ab.y                                     // directional albedo, float3
diffuseWeight = PathDiffuseEnergy(E_spec, NoV) per channel           // pathtracer_math.hlsli:46-50

sunShaded = albedo * (E_sun * (NoL * visibility * M_1_PI)) * diffuseWeight    // the existing Lambert, weighted
          + E_sun * (visibility * specularCos)                                  // the new lobe
```

- The probe-irradiance Lambert (`albedo * SdfReflectionIrradiance(...)` / `albedo * indirect`) and
  `emission` are unchanged in pass one (the brief: sun lobe only, no secondary gather). The asymmetry
  (the sun Lambert is weighted, the probe Lambert is not) is conservative; the full coupling is a
  follow-up (section 10).
- Energy: for the sun's contribution the hit's directional albedo is `albedo * diffuseWeight +
  E_spec_true`. With `diffuseWeight = max(0, 1 - E_spec_LUT - 0.005)` and the LUT under-estimating by
  at most 0.005 (D7's 378-case sweep, 10-d7-diffuse-specular-energy.md:11-17, :34-41) the total is
  <= 1: conserved, never amplified. The analytic fit is NOT used for this weight (D7 rejected it, :22-23).
- Magnitudes (F0 = 0.04; the fit is quoted here only to size the effect, the LUT is the source of
  truth): roughness 0.7 -> E_spec 0.025 at NoV 1, 0.051 at NoV 0.1, so the Lambert loses 2.5-5%;
  roughness 0.5 -> 0.03 / 0.155. The lobe adds up to 21% of the first bounce (report.md:155). Net
  positive everywhere the sun is visible; zero where it is not (`visibility` gates both the Lambert and
  the lobe).
- Constants: `F0 = kGiHitSpecularF0 = float3(0.04, 0.04, 0.04)` (= `ComputeF0(albedo, 0)`,
  brdf.hlsli:189-192; Sponza's metallic is 0 everywhere, report.md:107-109); `roughness` = the CVar
  (section 6.1); `kGiHitSpecularMaxBrdfCos = 1.0` (section 8.1 derives it). F90 = 1 (the
  `F_Schlick(f0, cos)` overload); the raster's authored F90 is not available at a hit.
- `NoL <= 0` or `visibility == 0`: the lobe is zero and the weighted Lambert is zero; return the
  existing value. LUT SRV == sentinel (before the IBL bake runs) or `roughness <= 0`: identity
  (`diffuseWeight = 1`, `specularCos = 0`) so an unbound LUT degrades to today's shading, never to a
  mis-weighted one.

Why not `(1 - F)` at the half vector instead of `(1 - E_spec)`: that is the pre-D7 coupling that let
white dielectrics reach 1.284 directional albedo at roughness 0.3 / NoV 0.1 (10-d7:5-7).

## 5. Exact insertion points

Rule for every site: the new code lives in an `#ifdef UZ_GI_HIT_SPECULAR` block whose `#else` branch
is the CURRENT text verbatim (not re-factored, not re-associated: `albedo * (A + B)` and `albedo*A +
albedo*B` round differently). The default pipeline therefore preprocesses to today's text.

### 5.1 New header `runtime/shaders/runtime/gi/gi_hit_specular.hlsli`

Requirements: `brdf.hlsli` in scope (both includers get it through `../lighting/ibl.hlsli`); includes
`../pathtracing/pathtracer_math.hlsli` for `PathDiffuseEnergy` (C++/HLSL-shared header, used from HLSL
at pathtracer_shared.hlsli:172-173, so it compiles under DXC). Contents: the two constants of section
4, a `GiHitSunSpecular { float3 specularCos; float3 diffuseWeight; }` result, and

```
GiHitSunSpecular EvalGiHitSunSpecular(float3 N, float3 V, float3 L, float NoL, float roughness,
                                      float3 F0, uint lutSrv, SamplerState lutSampler)
```

implementing section 4 with the identity early-outs (`lutSrv == 0xffffffffu`, `roughness <= 0`,
`NoV <= 0`), the LUT read as a `Texture2D<float2>` (the baker writes RG16F, brdf_lut_cs.hlsl:23-25;
the PT reads it as `Texture2D<float2>`, pathtracer_shared.hlsli:165) with `GetDimensions` for the
half-texel clamp (pathtracer_shared.hlsli:166-170). No `NonUniformResourceIndex` (uniform SRV).

### 5.2 Site A - sdf_reflections.hlsli

1. After the includes (:6-9): `#ifdef UZ_GI_HIT_SPECULAR` `#include "gi_hit_specular.hlsli"` `#endif`.
2. `struct SdfReflectionScene` (:32-56), after `surfaceCacheParamsSrv` (:55), under the define:
   `float hitSpecularRoughness; uint hitSpecularLutSrv;`. Only the gather shaders set them (5.5);
   the deferred PS never compiles the define so its construction (:1404-1423) is untouched.
3. `ShadeSdfReflectionHit`: after `albedo` (:255), open the define block and replace the tail
   :256-268 with:
   - `const float3 sunLambert = albedo * (scene.sunColor * (ndl * visibility * M_1_PI));`
   - `GiHitSunSpecular hs = EvalGiHitSunSpecular(normal, -rayDirection, scene.sunDirection, ndl,
     scene.hitSpecularRoughness, kGiHitSpecularF0, scene.hitSpecularLutSrv, UZ_SDF_REFLECTION_SAMPLER);`
   - `const float3 sunShaded = sunLambert * hs.diffuseWeight + scene.sunColor * (visibility * hs.specularCos);`
   - cellRead && DirectMode 1 (was :256-259): `return sunShaded + cellOther;`
   - cellWrite (was :260-266): `SurfaceCacheUpdate(..., sunLambert, otherTerm, kSurfaceCacheWriterSdf)` -
     the cell's `sun` layer is view-independent and must receive the UNWEIGHTED Lambert, never the
     lobe or the (view-dependent) weight; `return sunShaded + otherTerm;`
   - inline (was :267-268): `return sunShaded + albedo * SdfReflectionIrradiance(scene, origin, normal, hitT);`
   - `#else` + lines :256-268 verbatim + `#endif`.
   The early return at :249 (cell replaces the whole radiance) is unchanged in both branches (8.2).

### 5.3 Site B - dxr_diffuse_gather.hlsli

Replace the return at :90-91 with a define block: `sunLambert = albedo * (scene.sunColor * (ndl *
visibility * M_1_PI))`; `hs = EvalGiHitSunSpecular(normal, -direction, scene.sunDirection, ndl,
scene.hitSpecularRoughness, kGiHitSpecularF0, scene.hitSpecularLutSrv, UZ_SDF_REFLECTION_SAMPLER)`;
`return emission + sunLambert * hs.diffuseWeight + scene.sunColor * (visibility * hs.specularCos) +
albedo * SdfReflectionIrradiance(scene, position, normal, hitT);` with `#else` + :90-91 verbatim.
`normal` is unit and faces the ray (:72-74); the mode-2 control (:37-46) takes the same path. The
header is only compiled under UZ_GATHER_DXR, so `gi_hit_specular.hlsli` reaches it through
sdf_reflections.hlsli (included first, sdf_diffuse_gather.hlsli:6-8).

### 5.4 Site C - radiance_cache_trace_cs.hlsl

1. After `#include "../lighting/ibl.hlsli"` (:17): the define-guarded include of gi_hit_specular.hlsli.
2. Root constants (:23-57): see 6.4 (two DWORDs under the define, after the stability trio).
3. In the inline branch (:736-764), replace :756-763 with a define block:
   - `const float3 sunLambert = albedo * (sunRad / kPI);`
   - `GiHitSunSpecular hs = EvalGiHitSunSpecular(N, -dir, L, NdL, asfloat(g_hitSpecularRoughnessBits),
     kGiHitSpecularF0, g_hitSpecularLutSrv, linearClamp);`
   - `const float3 sunShaded = sunLambert * hs.diffuseWeight + float3(lightColorR, lightColorG,
     lightColorB) * lightIntensity * (visibility * hs.specularCos);` (`visibility` already carries the
     `dot(N, -dir) > 0` condition through :740; keep the :756 ternary form).
   - `radiance = dot(N, -dir) > 0.0 ? sunShaded + albedo * indirect : 0.0;`
   - stats: `pathStats.y += lum(sunShaded)` (the "sun-term luminance" record keeps meaning "what the
     sun put into this ray"), `pathStats.z += lum(albedo * indirect)` as today (:757-760);
     `traceStats.z` (:747) stays the un-shaded `sunRad` luminance.
   - DirectMode 1 (was :763): `if (cellRead) radiance = sunShaded + cellOther;`
   - `#else` + :756-763 verbatim + `#endif`.
   The cell-replaces-radiance branch (:729-735) is unchanged. The `UZ_RC_STABILITY` ray buffers
   (:778-784) receive the new `radiance` automatically, so the depth-moments pass and the stage-2
   visibility see the same ray the SH does.

### 5.5 Scene construction for the gather - ssgi/ssgi_world_gather.hlsli

`BuildSurfaceScene()` (:13-32), after `scene.surfaceCacheParamsSrv = ...` (:30), under the define:
`scene.hitSpecularRoughness = asfloat(UZ_SSGI_HIT_SPECULAR_ROUGHNESS_BITS); scene.hitSpecularLutSrv =
brdfLutSrvIndex;` with the same indirection pattern the surface cache uses (:6-11): the including
shader `#define`s `UZ_SSGI_HIT_SPECULAR_ROUGHNESS_BITS g_hitSpecularRoughnessBits` before the include
(ssgi_trace_cs.hlsl:40 is the precedent line). `brdfLutSrvIndex` comes from the PerFrameCB mirror
(5.6). `ProbeSurfaceLightingSnapped` (:56-62) and the identified gather (:40-51) go through the same
builder, so diagnostics stay consistent with the shipping gather.

### 5.6 PerFrameCB mirror - ssgi/ssgi_surface.hlsli

The sparse `packoffset` mirror (:9-45) does not declare the LUT today. Add, under the define, `uint
brdfLutSrvIndex : packoffset(c14.x);` (byte 224: constant_buffers.h:65; the exact precedent is
translucent_ps.hlsl:46). This keeps the LUT single-sourced; the RC trace has no PerFrameCB and gets
the SRV as a root DWORD instead (6.4).

## 6. Gating: CVars, permutations, constants, selection, OFF identity

### 6.1 CVars

- `r.GI.FinalGather.HitSpecular` - bool, default false. Namespace matches the existing gather CVars
  (`r.GI.FinalGather`, `.Samples`, `.SampleRotation` at frame_constants.cpp:84-91; `.StableTemporal`
  at ssgi_renderer.cpp:46; `.DXR` at rt_manager.cpp:53).
- `r.GI.FinalGather.HitSpecular.Roughness` - float, default 0.7, range [0.4, 1.0]. Sponza's ORM
  averages are 0.6-0.8 (report.md:156); the lower bound is where the firefly guard starts to matter
  (8.1).
- Register both in ssgi_renderer.cpp next to `cv_StableTemporal` (:46) and expose two statics
  (`SSGIRenderer::HitSpecularEnabled()`, `HitSpecularRoughness()`), the pattern of
  `RadianceCache::StabilityEnabled()` (radiance_cache.cpp:144-148), so frame_gi_passes.cpp (RC pipeline
  selection) and radiance_cache.cpp (root DWORDs) read one source of truth.

### 6.2 Permutations (`UZ_GI_HIT_SPECULAR=1`)

`ShaderManager::CreateShaderWithDefines(shader, stage, defines)` (shader_manager.cpp:147-172) takes a
`;`-separated define string (shader_pipeline.cpp:269-279) and keys the bytecode cache on source text +
sorted defines (:281-309), so combinations are supported as-is. Add to `PipelineLibrary`
(pipeline_gi.cpp), each lazily created like `GetSsgiDxrTrace` (:231-243) and
`GetRadianceCacheTraceStability` (:663-686):

| accessor | shader | defines |
|---|---|---|
| GetSsgiTraceHitSpecular | SsgiTraceCS | `UZ_GI_HIT_SPECULAR=1` |
| GetSsgiDxrTraceHitSpecular | SsgiTraceCS | `UZ_GATHER_DXR=1;UZ_GI_HIT_SPECULAR=1` |
| GetSsgiUpsampleHitSpecular | SsgiUpsampleCS | `UZ_GI_HIT_SPECULAR=1` |
| GetSsgiDxrUpsampleHitSpecular | SsgiUpsampleCS | `UZ_GATHER_DXR=1;UZ_GI_HIT_SPECULAR=1` |
| GetRadianceCacheTraceHitSpecular | RadianceCacheTraceCS | `UZ_GI_HIT_SPECULAR=1` |
| GetRadianceCacheTraceStabilityHitSpecular | RadianceCacheTraceCS | `UZ_RC_STABILITY=1;UZ_GI_HIT_SPECULAR=1` |

Not permuted in pass one: DeferredLightingPS (reflections consumer stays Lambert, 8.3), the three
ReSTIR GI pipelines (8.6), the surface-cache writers and the DDGI writer (8.2, section 10). The
shipped shader count ("Shader integrity OK (168 shaders verified)", report.md:7) is unchanged: these
are runtime variants, as the existing ones are.

### 6.3 Gather root constants (ssgi_trace_cs.hlsl, ssgi_upsample_cs.hlsl, ssgi_renderer.cpp)

Today: 14 DWORDs + 3 under `UZ_GATHER_DXR` (ssgi_trace_cs.hlsl:14-36; CPU `SSGITraceRC`
ssgi_renderer.cpp:713-733, pushed as 17 or 14 at :757); the upsample has 10 + 3 (:1315-1331, pushed
13 or 10 at :1352). Root parameter 0 holds 24 DWORDs (dx12_pso_creation.cpp:183-186), so one more fits.

- HLSL: after the DXR trio add `#ifdef UZ_GI_HIT_SPECULAR uint g_hitSpecularRoughnessBits; #endif`
  and widen the trio's guard to `#if defined(UZ_GATHER_DXR) || defined(UZ_GI_HIT_SPECULAR)` so the
  new DWORD sits at index 17 (trace) / 13 (upsample) in every variant that declares it. In the
  HitSpecular-only variant the trio is declared and unused (dxr_diffuse_gather.hlsli is not included
  without UZ_GATHER_DXR), which is harmless.
- CPU: append `uint32_t hitSpecularRoughnessBits` to `SSGITraceRC` (static_assert 72) and
  `UpsampleRC` (56); fill with the CVar's float bits (the `memcpy` pattern of :749-751); push count
  `hs ? 18 : (dxr ? 17 : 14)` and `hs ? 14 : (dxr ? 13 : 10)`.
- Pipeline selection: :696 and :1295-1296 become a 2x2 choice on (`m_gatherDxrMode`, HitSpecular).
  Follow the DXR precedent for a variant that fails to compile (ssgi_renderer.cpp:410-416 validates
  both the trace and upsample DXR pipelines before enabling the mode): enable HitSpecular for the
  frame only if BOTH gather variants are valid, else log once and run the plain pipelines.
- History: a change of the effective HitSpecular state must invalidate the gather's temporal history
  exactly as a DXR-mode change does (`m_hasHistory = false; m_restirHistoryValid = false;` with the
  mode log, ssgi_renderer.cpp:413-417), so the stable temporal accumulator never blends OFF frames
  with ON frames; otherwise the first seconds after a toggle measure the blend, not the term.

### 6.4 Probe-writer root constants (radiance_cache_trace_cs.hlsl, radiance_cache.cpp)

Today: 17 DWORDs + 3 under `UZ_RC_STABILITY` (:23-57); the CPU always pushes 20 from
`rootConstants[20]` (radiance_cache.cpp:1041-1048, :1077-1080, :1086, :1108).

- HLSL: after the stability trio (:49-56) add `#if UZ_GI_HIT_SPECULAR uint
  g_hitSpecularRoughnessBits; uint g_hitSpecularLutSrv; #endif`, widening the trio's guard to
  `#if UZ_RC_STABILITY || UZ_GI_HIT_SPECULAR` (same index-stability argument as 6.3).
- CPU: `rootConstants[22]`; `[20]` = roughness bits, `[21]` = `pf->brdfLutSrvIndex` (the per-frame
  struct is already read there: `pf->prefilteredSrvIndex` at :1054); push 22 at both call sites.
  No change to `RadianceCacheTraceConstants` (radiance_cache.h:438-537) - the CB layout and its
  static_asserts are untouched, which is why root DWORDs are preferred over a row 32.
- Pipeline selection in frame_gi_passes.cpp:364-371: choose among the four RC trace pipelines on
  (`RadianceCache::StabilityEnabled()`, `SSGIRenderer::HitSpecularEnabled()`), keeping the existing
  "variant that failed to compile falls back" behaviour with a one-time log so an ON run that silently
  lost the cache term is visible in the log (and in `rt_d4_hs`, 9.3).

### 6.5 OFF byte-identity

- Shader text: every change is inside `#ifdef UZ_GI_HIT_SPECULAR` with the current text in `#else`
  (section 5), or is an added declaration under the define (5.2 item 2, 5.6, 6.3, 6.4). The default
  pipelines preprocess to today's text; their DXIL is identical up to debug metadata (the bytecode
  cache key changes because the SOURCE text changed - expect one recompile, then compare the stripped
  bytecode hashes of the default pipelines before/after).
- CPU: with the CVar off, the pipeline handles chosen at ssgi_renderer.cpp:696/:1295 and
  frame_gi_passes.cpp:367 are the existing ones and the pushed DWORD counts are the existing ones.
- Render: 9.2 re-runs the archived conditions and requires 5-decimal identity.

## 7. Probe-writer parity

The cache's fixed point is depressed by the same omission (report.md:159-161, :204-209): the probe
rays shade their hits Lambert-only (2.4) and the gather reads the cache at ITS hits
(`SdfReflectionIrradiance`, sdf_reflections.hlsli:169-209). Section 5.4 puts the identical term, with
the identical constants, into the writer so cache and gather agree:

- Same roughness CVar, same F0, same LUT (passed as a root DWORD, 6.4), same `PathDiffuseEnergy`.
- V = -dir is the probe ray direction: the SH stores radiance arriving at the probe, so the lobe
  "as seen from the probe" is exactly the quantity the SH integrates; receivers interpolate the SH as
  they do today. No new approximation beyond the one the diffuse term already makes.
- Variance: 64 uniform-sphere rays per probe (radiance_cache_trace_cs.hlsl:171, :630) EMA-blended. The
  stage-1 change detector normalises its threshold by the per-ray luminance variance of the SAME
  update (:874-877, :892-898), so a brighter-tailed ray distribution raises the threshold rather than
  causing re-arms. Measured in 9.3 (`rt_d4_hs`) and with the stage-2 crawl scripts.
- The DDGI atlas writer (ddgi_probe_trace_cs.hlsl:139-147) is a separate cache with the same Lambert
  form; it is not part of this change (section 10) and the DDGI/RC selection is unaffected.

## 8. Risks

### 8.1 Fireflies from a tight lobe at grazing hits - bounded

`D * Vis * NoL` (the Fresnel-free part of `f_spec * cos`) at the worst configuration a gather ray can
land on (H = N, NoV = NoL = 0.05, so VoH = 0.05 and F = 0.78):

| roughness | alpha | D(NoH=1) | Vis(0.05, 0.05) | D*Vis*NoL | x F | note |
|---|---|---|---|---|---|---|
| 0.7 | 0.49 | 1.33 | 10.2 | 0.67 | 0.53 | never clamped; ~1.7x a white Lambertian's 0.318 at normal incidence |
| 0.5 | 0.25 | 5.09 | 19.6 | 5.0 | 3.9 | clamped to 1.0 at the grazing-mirror configuration only |
| 0.4 | 0.16 | 12.4 | 29.9 | 18.5 | 14.5 | clamped |
| 0.3 | 0.09 | 39.3 | 48.6 | 95 | 75 | why the CVar's lower bound is 0.4 and the guard exists |

(`D_GGX` brdf.hlsli:24-29, `V_SmithGGXCorrelated` :94-100; at normal incidence roughness 0.7 gives
0.33 x F(0.04) = 0.013.) So: `kGiHitSpecularMaxBrdfCos = 1.0` bounds a hit to at most `F * E_sun *
visibility` <= `E_sun` (a white Lambertian returns `E_sun / pi`); it is a no-op at the default
roughness, so it introduces no bias in the validated configuration and only trims the low end of the
CVar range. The report's "clamp alpha >= 0.1" (report.md:172) is NOT sufficient on its own (row 0.3
above is alpha 0.09; alpha 0.1 still reaches ~55). The variance does enter the gather's 8-ray mean
and the stable temporal accumulator; 9.3 measures the per-ray luminance tail and the ROI temporal std.
The term adds no random dimension: the lobe is a deterministic function of the ray the gather already
traced (report.md:23, :180).

### 8.2 Double counting - none in the inline paths; the cell paths are OFF-equivalent

- Receiver: the gather returns incident radiance that the lighting pass treats as E/pi
  (deferred_lighting_ps.hlsl:1524-1529) and weights with the receiver's own diffuse and multi-scatter
  terms (:1496-1497, :1522). The receiver's own direct sun specular (:870-876) is a different light
  path. A brighter `cosineIrradiance` raises `cosineIrradiance * multiScatter` proportionally - that
  is the receiver's BRDF applied to more incident light, not a double count.
- SDF reflections (deferred PS, `GatherSdfReflections` :1438) and RT reflections
  (`ReflectionHitRadiance`, rt_reflection_contract.hlsli:185-187) keep Lambert hits: no define there.
  The separate specular over-estimate at floor contacts the report noted (report.md:225-226, :83-88)
  is in that path and is not made worse.
- SSR reflects the lit buffer (which already contains receivers' direct highlights); unchanged.
- Surface cache ON (engine default OFF, surface_radiance_cache.cpp:34 per report.md:46-48): a readable
  cell replaces the whole hit radiance (sdf_reflections.hlsli:249; radiance_cache_trace_cs.hlsl:
  729-735; D7 reader policy surface_cache_contract.hlsli:393) and the cells' `sun` layer is Lambert
  (`SurfaceCacheDirectLayer`, surface_cache_hit.hlsli:122-128). Under HitSpecular ON those hits stay
  Lambert: no double count, but an inconsistency between cell-read and inline hits. DirectMode 1
  (`kSurfaceCacheFlagDirectInline`, surface_cache_contract.hlsli:108) shades the sun inline and gets
  the term (5.2). The SdfWrites diagnostic writes the unweighted Lambert into the cell (5.2) because
  the cell is view-independent. Documented limitation; the fix is Design C in the cells (section 10).

### 8.3 Interaction with RoughReconstruct and the reflections path

`r.RayTracing.Reflections.RoughReconstruct` (frame_gi_passes.cpp:79-80) reconstructs rough RT
specular from the RT reflection pass before TAA (`RegisterRoughSpecularReconstruction`, :985-1000);
its input is the reflection pass whose hit shading is unchanged (8.2). The diffuse gather output is
not an input to it. No interaction beyond the receiver-side multi-scatter already covered.

### 8.4 Metallic hits

The clipmap albedo is `(1 - metallic)`-weighted (global_sdf_composite_cs.hlsl:60-61) and the material
table has no metallic (2.5), so a metal hit is black today and, with F0 = 0.04, gets only the
dielectric lobe under ON: still under-lit, never over-lit or wrongly tinted. Correct metals need
F0 = lerp(0.04, baseColor, metallic) and diffuse x (1 - metallic) at the hit, i.e. per-mesh (roughness,
metallic) in `GIMeshMaterial.padding` (gi_hit_material.hlsli:36; CPU mirror `GIMeshMaterialGPU`,
path_tracer_pass.h per :20) for the DXR path - the SDF path has no mesh id at the hit and stays
dielectric. Follow-up (section 10); Sponza has no metals (report.md:107-109).

### 8.5 Energy asymmetry in pass one

Only the sun Lambert is weighted by `(1 - E_spec)`; the probe-irradiance Lambert is not and no
specular reflection of the probe irradiance is added. Conservative (nothing is amplified); the
complete coupling (`albedo * E/pi * diffuseWeight + E_spec * E/pi`, Lumen's :950-953 shape) is worth
roughly `E_spec * (1 - albedo) * E/pi` = 1-3% of the probe term and is a follow-up.

### 8.6 ReSTIR GI

The ReSTIR pipelines include the same gather code but are not permuted in pass one (6.2): with
`r.GI.ReSTIR=1` and HitSpecular ON, candidates and re-traces are shaded without the term. Log once
("HitSpecular not applied under r.GI.ReSTIR=1") and validate the plain path, which is the engine
default (report.md:46-49) and the harness baseline. Adding the three ReSTIR variants later is
mechanical (`SSGIRestirRootConstants` is 16 DWORDs, ssgi_renderer.h:140; the include is
ssgi_restir_gi.hlsli:48).

### 8.7 Scene-constant roughness

One roughness for every hit mis-shapes the lobe on surfaces far from it (sharper floors get too wide
a lobe and vice versa) but, with the LUT weight evaluated at the SAME roughness, every hit stays
energy-consistent with itself. The sweep in 9.3 picks the default; the per-mesh table is the fix.

## 9. Validation protocol

Harness: `E:/UzmiEngine/_evidence/gi/indirect-underlit/` - `capture.py` (fixture + CVar sets),
`matrix.py` (conditions; `python matrix.py <names>` reruns named ones, matrix.py:1-5, :72-84),
`table.py` -> `results.json` / `tables.txt`. Fixture: Sponza Camera_A, 1920x1080, frames 90-150 for
real time, PT accumulated to frame 300 (report.md:33-44). `NOSKY = {"r.SkyLight.Intensity": 0}`
removes the sky from both renderers (matrix.py:47). The PT shaders are not touched, so every `pt_*`
run stays cached.

### 9.1 Gates

- `zig build -Doptimize=ReleaseFast` exit 0; shader integrity count unchanged (168).
- `npm run test:layouts` (`scripts/rendering/test_cbuffer_layouts.py`, package.json:21): Tier A
  offsets (the added `packoffset(c14.x)` mirror must match constant_buffers.h:65), Tier B root-block
  DWORD counts (update the inventory if it enumerates the gather / RC root blocks; the default-variant
  counts are unchanged).
- `npm test`.

### 9.2 OFF is byte-identical

- Stripped-bytecode hashes of the default SsgiTraceCS / SsgiUpsampleCS (plain + DXR) and
  RadianceCacheTraceCS (plain + stability) pipelines: equal before/after (6.5).
- `python matrix.py rt_full rt_restir0 rt_dxr1 rt_dxr_k0_nosky rt_d4` with the new binary and the
  CVar at its default, then `table.py`: every ROI equals the archived value to 5 decimals (the
  launch-to-launch floor, report.md:83).

### 9.3 ON acceptance - add these conditions to `RT` in matrix.py (:16-44 is the pattern)

| condition | CVars | compares against | accept |
|---|---|---|---|
| `rt_dxr_k0_nosky_hs` | rt_dxr_k0_nosky + `r.GI.FinalGather.HitSpecular 1` | `pt_b2_nosky - pt_b1_nosky` (sun-only first bounce) | aggregate ratio from 0.48 (report.md:177-179) to >= 0.58 and <= 0.70 (the PT's specular share bounds the gain at x1.27 -> 0.61 if the gather captures the lobe as efficiently as the diffuse); no ROI > 1.0; `shadow_floor` / `pillar_base_l` lift <= the aggregate lift (the term is visibility-gated; the contact leak is the feed's, report.md:129-132) |
| `rt_dxr_k0_hs` | rt_dxr_k0 + HS | `ptdiag`/`pt` first bounce as 4a(b) | consistent with the nosky run (sky is 15-16% of the PT indirect, report.md:100-103) |
| `rt_plain_hs` | rt_restir0 + HS | PT indirect (b8 - b1) | aggregate from 0.590 (report.md:81) toward ~0.65-0.70: the SDF path AND the probe writer carry the term |
| `rt_dxr1_hs` | rt_dxr1 + HS | PT indirect | from 0.483 toward ~0.55-0.60 |
| `rt_d4_hs` | rt_d4 + HS (cache read directly, matrix.py:27-28) | rt_d4 | walls/vaults rise by a factor in [1.05, 1.30] -> the RC variant is selected and the fixed point moved; equality to rt_d4 = the variant was not applied (6.4 fallback) |
| `rt_plain_hs_g` | rt_plain_g + HS ("gather" fields: per-ray records, sdf_diffuse_gather.hlsli:119-122) | rt_plain_g | per-ray luminance p99.9 ratio <= 1.5 and max ratio <= 3 over the 14 ROIs x 61 frames; per-ROI temporal std of `diffuse_indirect` <= 1.2x OFF; zero isolated pixels > 8x the ROI mean (the H-68 sparkle precedent, sdf_reflections.hlsli:318-327) |
| roughness sweep | `rt_dxr_k0_nosky_hs` at `.Roughness` 0.5 / 0.7 / 0.9 | per-ROI PT sun-only first bounce | pick the default minimising the per-ROI ratio spread; record it in this doc (expected 0.7) |
| lit control | all ON runs | `lit_floor` direct 0.2917 (report.md:104-106) | unchanged: direct lighting is not touched |
| stage-2 crawl/leak | the RC stability lane's scripts (docs/analysis/lumen-radiance-cache-stability/02, 04; `_evidence/gi/radiance-cache-stage2/`) on an ON run | their archived OFF baselines | no new crawl class; leak metrics unchanged (the term adds nothing where the sun is not visible) |

Interpretation guard: the remaining first-bounce diffuse shortfall (section 4a(b), 0.65x, mechanism
unidentified, report.md:228-238) caps what this lever can show; a result of ~0.6x on
`rt_dxr_k0_nosky_hs` is success, not failure.

## 10. Out of scope / follow-ups (ranked)

1. Per-mesh roughness and metallic for the DXR gather (`GIMeshMaterial.padding` as two fp16 averages,
   populated from the same ORM sources the PT material uses) - fixes 8.4 and 8.7 on the exact path.
2. Full coupling at the hit (8.5): weight the probe-irradiance Lambert and add `E_spec * E/pi`.
3. Surface-cache cells: Design C's view-independent fold into the cells' `sun` layer
   (`SurfaceCacheDirectLayer`, surface_cache_hit.hlsli:122-128) so cell-read hits stop lagging
   inline hits when `r.GI.SurfaceCache=1`; or an inline visibility re-trace under DirectMode 1.
4. ReSTIR GI variants (8.6).
5. DDGI writer parity (ddgi_probe_trace_cs.hlsl:139-147) if that cache feeds receivers in a scene.
6. A per-mesh roughness channel in the SDF composite for the SDF path (report.md:164-166).

## 11. One-pass checklist for the implementation lane

1. New `gi/gi_hit_specular.hlsli` (5.1).
2. sdf_reflections.hlsli: include, struct fields, `ShadeSdfReflectionHit` tail under the define with
   the verbatim `#else` (5.2).
3. dxr_diffuse_gather.hlsli:90-91 under the define (5.3).
4. radiance_cache_trace_cs.hlsl: include, root DWORDs with the widened guard, :756-763 under the
   define (5.4, 6.4).
5. ssgi_world_gather.hlsli `BuildSurfaceScene` fields + the `UZ_SSGI_HIT_SPECULAR_ROUGHNESS_BITS`
   indirection; ssgi_trace_cs.hlsl / ssgi_upsample_cs.hlsl root DWORD + `#define` before the include;
   ssgi_surface.hlsli `brdfLutSrvIndex : packoffset(c14.x)` (5.5, 5.6, 6.3).
6. ssgi_renderer.cpp: two CVars + statics (6.1), `SSGITraceRC` / `UpsampleRC` + push counts, 2x2
   pipeline selection with the both-variants-valid rule (6.3).
7. pipeline_gi.cpp: six lazy accessors (6.2).
8. radiance_cache.cpp: `rootConstants[22]`, `[20]`/`[21]`, push 22 (6.4); frame_gi_passes.cpp:
   four-way RC pipeline selection with the one-time fallback log (6.4).
9. test_cbuffer_layouts.py inventory if needed (9.1); matrix.py new conditions (9.3).
10. Run 9.1, 9.2, 9.3; record the chosen roughness default and the measured ratios in a short
    section 12 of this document.

## 12. Implementation record (2026-11-02; evidence in E:/UzmiEngine/_evidence/gi/hit-vertex-specular/report.md)

- Implemented as specified (sections 4-6): `gi/gi_hit_specular.hlsli`, the three sites under `UZ_GI_HIT_SPECULAR`
  with the previous text verbatim in `#else`, root DWORDs 17/13 (gather trace/upsample) and 20-21 (RC trace) with the
  widened guards, `r.GI.FinalGather.HitSpecular` (0) + `.Roughness` (0.7, [0.4, 1]), the six lazy permutations,
  history invalidation on toggle, the layout-test entry for the `brdfLutSrvIndex` mirror.
- OFF: the 21 DXIL blobs of every reader of a touched header are SHA-256 identical to the baseline's; rendered ROI
  means equal the baseline within the same-exe launch-noise envelope (rt_d4 set A bit-identical).
- ON (Sponza Camera_A, roughness 0.7): `rt_dxr_k0_nosky` 0.485 -> 0.641x of the PT sun-only first bounce (9.3's
  0.58-0.70 window); `rt_dxr1` 0.483 -> 0.583x of the PT indirect; the engine-default plain gather
  (ReSTIR 0, SurfaceCache 0) 0.471 -> 0.592x; `rt_d4` x1.31 (variant applied). With `r.GI.SurfaceCache=1` the SDF
  path lifts only x1.03 (8.2: cells replace the hit). Direct lighting unchanged.
- Variance (9.3's `rt_plain_hs_g` row): per-ray p99.9 ON/OFF <= 1.5 on 10-11 of 14 ROIs but 1.9-2.9 on 3
  (contact_right, pillar_base_r, back_arch); max-ray ratio up to 4.5; ROI temporal std up to 1.73x (4 ROIs > 1.2x,
  launch floor 1.04). Sparkles (> 8x ROI mean) do not increase (OFF 40-47, ON 40-49). At roughness 0.9 every variance
  gate passes (p99.9 max 1.43, max-ray 1.37, std max 1.24) with ~half the lift (x1.14 vs x1.26). Default kept at 0.7
  per this spec; the roughness sweep's per-ROI ratio spread does not separate 0.5 (0.242) from 0.7 (0.244); 0.9 = 0.265.
- Firefly clamp (8.1 corrected): the hemisphere maximum of `D*Vis*NoL` is not the H = N case; it is 1.24 at
  roughness 0.7 (V 89.5 deg, L 82 deg, opposite azimuths), 32 at 0.4, 0.89 at 0.74, so the cap at 1.0 trims a
  grazing sliver by <= 19% at the default and is a no-op from 0.74 up.
