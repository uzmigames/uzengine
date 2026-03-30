<!-- doc-meta: category=analysis, area=falcor, topic=shader-pbr-improvements -->

# Shader Improvement Table — PBR/BRDF vs Falcor

Deep function-by-function comparison of UzEngine PBR shaders against Falcor's known implementations.

---

## Summary of Critical Gaps

1. `lighting.hlsli` is a **stub** — only evaluates the first directional light with no real dispatch. The real lighting is in `deferred_lighting_ps.hlsl` and is non-reusable.
2. `light_types.hlsli` is a **stub** — 5 float4s with no semantic fields, used by nobody except the dead stub.
3. `D_GGX` has no alpha=0 delta-BSDF guard — produces `NaN/Inf` for mirror surfaces.
4. `Fd_Burley` missing Frostbite energy-conservation factor `/ 1.51` — diffuse over-bright at high roughness.
5. `ImportanceSampleGGX` uses NDF sampling — Falcor uses VNDF (Heitz 2018) for lower variance.
6. `octahedral.hlsli` uses non-equal-area octahedral mapping — Falcor and Filament recommend equal-area.
7. No conductor Fresnel (`evalFresnelConductor`) — metals use Schlick approximation only.
8. No PCG/blue-noise per-pixel randomness utility — IBL baker and any future stochastic pass would benefit.
9. No `branchlessONB` (Pixar 2017) for TBN construction from a single normal — GBuffer relies on mesh tangents.
10. No multi-scatter IBL for diffuse lobe (only specular multi-scatter via Kulla-Conty is present).
11. No Oren-Nayar diffuse model — rough non-metallic surfaces use Burley only.
12. `math_util.hlsli` missing: `M_2PI`, `M_1_PI`, `M_4PI`, `M_INV_PI` — constants duplicated across shaders.
13. `ibl.hlsli` missing `SolidAngleSample`, `ComputeEnvLOD` — referenced in spec but absent.
14. No material ID dispatch in deferred pass — all shading models evaluated inline via `if (shadingModelID == N)` branches.

---

## Detailed Function Table

| File | Function | Current Implementation | Falcor Equivalent | Improvement Opportunity | Priority |
|------|----------|----------------------|-------------------|------------------------|----------|
| `pbr_common.hlsli` | `D_GGX` | `a2 / (PI * f * f)` — no guard on alpha=0 | `evalNdfGGX` — clamps `alpha >= 1e-3`, falls back to delta BSDF | When alpha < 1e-3 (mirror surfaces), `f = 1.0 + (a2-1)` evaluates to `(1 + a2 - 1)^2 = a2^2` which is valid but `1/(PI * a2^2)` diverges as alpha→0, producing numerical noise. Add clamp: `if (alpha < 1e-3) return 0;` and handle mirror as delta in the path tracer path. | P1 |
| `pbr_common.hlsli` | `V_SmithGGXCorrelated` | Exact Heitz 2014 eq.99 with `1/(2*(lV+lL))` form | `evalMaskingSmithGGXCorrelated` — same Heitz exact form | Implementation is correct. No change needed for rasterization. VNDF sampling (`ImportanceSampleVNDF`) is a separate concern. | P3 (no change) |
| `pbr_common.hlsli` | `F_Schlick` | `F0 + (1-F0) * (1-cosTheta)^5` — dielectric/metallic only | `evalFresnelSchlick` (dielectric) + `evalFresnelConductor(eta, k, cosTheta)` (conductor) | Missing conductor Fresnel for complex IOR materials (gold, copper). Conductor Fresnel: `F = ((n-1)^2 + k^2) / ((n+1)^2 + k^2)` at normal incidence. Enables physically correct colored specular without texture-baked F0. | P2 |
| `pbr_common.hlsli` | `F_SchlickRoughness` | Standard Lagarde 2011 form | Same — both use `max(1-roughness, F0)` | Correct. No change needed. | P3 (no change) |
| `pbr_common.hlsli` | `Fd_Burley` | `(1/PI) * (1 + (fd90-1)*Pow5(1-NdotL)) * (1 + (fd90-1)*Pow5(1-NdotV))` | Falcor/Frostbite variant: same formula then `* (1.0 / 1.51)` for energy conservation | Missing the Frostbite energy-conservation renormalization by `1/1.51`. At roughness=1, Burley diffuse integrates to ~1.51*albedo (not 1.0*albedo), violating energy conservation. Add `return albedo * (1.0 / PI) * ls * vs / 1.51;` — @ref Lagarde 2014 "Moving Frostbite to Physically Based Rendering 3.0" | P1 |
| `pbr_common.hlsli` | `EvaluateBRDF` / `EvaluateBRDFScaled` | Passes `sqrt(alpha)` as roughness to `Fd_Burley` — double-conversion | Falcor passes perceptual roughness directly to `evalDiffuseFrostbiteDisney` | `alpha = roughness^2` already in `EvaluateBRDF`. Then `Fd_Burley(albedo, sqrt(alpha), ...)` converts back to perceptual roughness. This is correct but redundant. Simplify to pass `roughness` (perceptual) directly, matching the original Burley formula which expects perceptual roughness. | P2 |
| `pbr_common.hlsli` | `ComputeF0` | `lerp(0.08*specular, albedo, metallic)` | `computeDielectricF0` + metallic blend | Correct pattern. `0.08 * specular` maps artist [0,1] to [0, 0.08] dielectric F0 range, matching UE4 convention. No change needed. | P3 (no change) |
| `pbr_common.hlsli` | `AOMultiBounce` | Jimenez 2016 polynomial for multi-bounce AO | Same polynomial — used identically | Correct. No change needed. | P3 (no change) |
| `pbr_common.hlsli` | `ACESFilm` | Narkowicz 2015 approximation (fitted curve) | Falcor uses full ACES RRT+ODT via LUT or Hill 2020 exact matrices | Narkowicz is a low-cost approximation. Falcor uses the Academy-published RRT+ODT matrix path. This is tracked separately in UzEngine's film tone operator (PerFrameCB has `filmSlope`, `filmToe`, etc). The ACES approximation here is only used by the forward mesh shader which is a stub — no immediate concern. | P3 |
| `pbr_common.hlsli` | (missing) | No sheen lobe | `SheenBRDF` — Estevez & Kulla 2017 Charlie NDF + Neubelt visibility | `D_Charlie` + `V_Neubelt` exist in `deferred_lighting_ps.hlsl` (smID==7 path) but are NOT in `pbr_common.hlsli`. Move these shared functions to `pbr_common.hlsli` so they are reusable. | P2 |
| `pbr_common.hlsli` | (missing) | No Oren-Nayar | `evalDiffuseOrenNayarBRDF(albedo, roughness, NoV, NoL, VoH)` | Falcor includes Oren-Nayar for rough non-metallic diffuse as an alternative to Burley. Oren-Nayar is more physically accurate for rough diffuse (cloth, plaster, moon). Not critical for PBR metals/dielectrics. | P3 |
| `lighting.hlsli` | `AccumulateDirectLighting` | **STUB** — evaluates only `lights[0]` directional light, no attenuation, no loop | Full punctual light loop: point (inverse-square + windowed), spot (cone), directional | This file is a dead stub. All real lighting is in `deferred_lighting_ps.hlsl`. The stub is never called in the deferred pipeline. Should either be removed or converted to a proper shared include used by both the forward `mesh_ps.hlsl` and the deferred pass. | P1 |
| `light_types.hlsli` | `LightData` | **STUB** — `float4 position, direction, color, params` — no semantic fields, no type discriminator | `LightData` in Falcor: `position`, `direction`, `intensity`, `openingAngle`, `cosOpeningAngle`, `penumbraAngle`, `shadowIndex`, `type` enum | Never used in the deferred pipeline. `deferred_lighting_ps.hlsl` defines `PointLightData` and `SpotLightData` inline. Unify: move the real structs to `light_types.hlsli` and include from `deferred_lighting_ps.hlsl`. | P1 |
| `ibl.hlsli` | `ImportanceSampleGGX` | NDF importance sampling (samples half-vector from `p(H) ~ D(H)*NdotH`) | VNDF sampling (Heitz 2018) — samples visible normals: `p(H) ~ D(H)*G1(V,H)*VdotH/NdotV` | NDF sampling generates many samples in directions that `G1` masks (behind the microsurface from view). VNDF sampling eliminates these wasted samples: same quality at 2-4× fewer samples. Critical for IBL baker compute shader quality/cost. @ref Heitz 2018 "Sampling the GGX Distribution of Visible Normals" (JCGT vol7 no4). | P2 |
| `ibl.hlsli` | `RadicalInverseVdC` / `Hammersley` | Correct Van der Corput bit-reversal + Hammersley 2D | Identical — Falcor uses the same GLSL/HLSL reference implementation | Correct. No change needed. | P3 (no change) |
| `ibl.hlsli` | `CubeFaceDirection` | Correct DX cubemap face→direction mapping | Identical convention in Falcor | Correct. No change needed. | P3 (no change) |
| `ibl.hlsli` | `WorldToCubemap` | `float3(dir.x, -dir.z, dir.y)` — Z-up to Y-up cubemap swizzle | Falcor is Y-up natively — no swizzle needed | UzEngine is Z-up world space, DX TextureCube is Y-up. The swizzle is correct and necessary. No change needed. | P3 (no change) |
| `ibl.hlsli` | (missing) | No `SolidAngleSample(uint i, uint N, float3 dir)` | `Falcor: solidAngleSample` — weight for importance-sampled contribution | Referenced in `shader-library.md` spec but not implemented. Needed for correct IBL baker integration weight. | P2 |
| `ibl.hlsli` | (missing) | No `ComputeEnvLOD(roughness, resolution)` | Falcor: `getTextureLod(roughness, numSamples, resolution)` | Referenced in spec, absent from code. Used for pre-filtered specular mip selection in the real-time path. Currently `deferred_lighting_ps.hlsl` uses `roughness * (prefilteredMipLevels - 1)` directly — ad hoc. Centralize in `ibl.hlsli`. | P2 |
| `math_util.hlsli` | PI constant | `static const float PI = 3.14159265358979f` | Falcor defines `M_PI`, `M_2PI`, `M_1_PI`, `M_4PI`, `M_INV_PI`, `M_INV_4PI`, `M_SQRT2` | Only `PI` is defined. `deferred_lighting_ps.hlsl` has three inline `3.14159265` literals (lines 217, 409, 420). Falcor centralizes these. Add `M_2PI`, `M_1_PI`, etc. and replace all inline float literals. | P2 |
| `math_util.hlsli` | `Pow2/4/5` | Scalar `float` only | Falcor has vectorized `pow2`, `pow4`, `pow5` for `float`, `float2`, `float3`, `float4` | No blocking issue for current code. Add overloads if vectorized versions are needed (e.g., for per-channel roughness in anisotropic materials). | P3 |
| `math_util.hlsli` | (missing) | No `branchlessONB(N, T, B)` | Pixar 2017 ONB: single normal → orthogonal TBN without cross product chain | `gbuffer_ps.hlsl` builds TBN from mesh tangent (interpolated from vertices). For procedural normals or screen-space passes without tangents, a branchless ONB would allow TBN construction from N alone. @ref Duff 2017 "Building an Orthonormal Basis, Revisited" (JCGT vol6 no1). | P2 |
| `math_util.hlsli` | (missing) | No PCG hash | Falcor: `uint pcg(uint v)` + `float2 pcg2(uint2 v)` for per-pixel noise | Any stochastic effect (VNDF sampling, blue-noise dithering, AO jitter) requires a fast per-pixel random source. PCG hash is widely accepted as the reference quality/speed tradeoff. | P2 |
| `math_util.hlsli` | `LinearToSrgb` / `SrgbToLinear` | Correct piecewise IEC 61966-2-1 | Identical in Falcor | Correct. No change needed. | P3 (no change) |
| `octahedral.hlsli` | `OctEncode` / `OctDecode` | Non-equal-area octahedral encoding (Narkowicz 2014 / Cigolle 2014) | Falcor/Filament use equal-area octahedral (Cigolle 2014 §3.2, Clarberg 2008) | Current encoding has non-uniform angular error distribution — error peaks at octahedron edges/corners. Equal-area encoding spreads error uniformly. Replace with: `float2 OctEncode(float3 n) { n /= (abs(n.x)+abs(n.y)+abs(n.z)); return n.z >= 0.0 ? n.xy : OctWrap(n.xy); }` where `OctWrap` uses `sign`-based fold. @ref Cigolle 2014 "A Survey of Efficient Representations for Independent Unit Vectors" (JCGT). | P2 |
| `octahedral.hlsli` | `OctEncode` | Uses branching `if (n.z < 0.0)` | Falcor uses branchless `sign()`-based fold | Replace `if` with `sign()` for GPU-friendly scalar execution: `float2 s = sign(o); o = (1.0 - abs(o.yx)) * s;` — already present in `gbuffer_ps.hlsl` inline version which is slightly different from `octahedral.hlsli`. Unify them. | P2 |
| `gbuffer_ps.hlsl` | Normal encode (inline) | Inline duplicate of `OctEncode` — does NOT call `octahedral.hlsli` | Should call `OctEncode` from `#include "../include/octahedral.hlsli"` | `gbuffer_ps.hlsl` does not `#include "octahedral.hlsli"` and has its own inline octahedral encode at lines 176-183. This is a divergence bug — if `octahedral.hlsli` is updated, `gbuffer_ps.hlsl` will not inherit the fix. Add `#include "../include/octahedral.hlsli"` and replace inline code with `OctEncode(N)`. | P1 |
| `gbuffer_ps.hlsl` | TBN construction | `float3 B = cross(N, T) * tangentWS.w` — standard Gram-Schmidt | Same pattern in Falcor for rasterization | Correct for textured meshes. Add `branchlessONB` fallback for meshes without tangents (detected via `tangentWS == float4(0,0,0,0)`). | P2 |
| `gbuffer_ps.hlsl` | Material packing | `smID` packed as `RT3.a = specular + smID * 16.0` — 4-bit smID in a float channel | Falcor uses a dedicated `materialID` GBuffer channel (integer texture) | Current 4-bit encoding limits shading models to 16 types. Encoding into a float channel loses precision if `specular` value rounds incorrectly. Consider moving to a dedicated `R8_UINT` material ID render target, or using integer packing via `asuint/asfloat`. | P3 |
| `gbuffer_ps.hlsl` | (missing) | No anisotropic tangent stored | Falcor stores tangent frame in GBuffer for anisotropic materials | Required for anisotropic GGX (brushed metal, hair). Needs additional RT or packed encoding. | P3 |
| `deferred_lighting_ps.hlsl` | `Vis_SmithJointApprox` | Epic's fast approximation (Karis 2013) — separate from `V_SmithGGXCorrelated` in `pbr_common.hlsli` | Falcor uses the exact Heitz form always | Two different visibility functions are used: exact Heitz in `pbr_common.hlsli`, Epic approximation in `deferred_lighting_ps.hlsl`. Both should use the same function. The approximation introduces ~1-3% error for rough materials — acceptable but inconsistent. Decide on one function and use it everywhere. | P2 |
| `deferred_lighting_ps.hlsl` | `D_Charlie` + `V_Neubelt` | Defined locally in `deferred_lighting_ps.hlsl` | Falcor has these in a shared `ShadingModels.hlsli` | Move `D_Charlie` and `V_Neubelt` to `pbr_common.hlsli` for reusability. Currently unreachable from any forward path. | P2 |
| `deferred_lighting_ps.hlsl` | Multi-scatter IBL (specular) | Kulla-Conty 2017 + Fdez-Aguera 2019: `FmsEms = Ems * FssEss * Favg / (1 - Favg * Ems)` + `specularAmbient += FmsEms * irradiance * 0.5` | Falcor: same Fdez-Aguera formulation | Specular multi-scatter is correctly implemented. No change needed. | P3 (no change) |
| `deferred_lighting_ps.hlsl` | Multi-scatter IBL (diffuse) | No multi-scatter compensation for diffuse lobe | Falcor: `diffuseAmbient *= 1.0 + FmsEms * (1.0 - kD_env)` — or Turquin 2019 coat | Diffuse multi-scatter is absent. Rough dielectrics lose energy because IBL diffuse does not account for multiple specular bounces returning energy to the diffuse lobe. Lower priority than specular. @ref Turquin 2019 "Practical multiple scattering compensation for microfacet models". | P3 |
| `deferred_lighting_ps.hlsl` | IBL mip selection | `mipLevel = roughness * (prefilteredMipLevels - 1)` — linear roughness-to-mip | Falcor: perceptual roughness mapped through `log2` based on NDF lobe width | Linear mapping undersamples specular lobe width at low roughness (too few mips for high-frequency specular). Correct mapping: `mipLevel = log2(roughness * resolution * sqrt(3.0 / sampleCount))`. Centralize in `ComputeEnvLOD` in `ibl.hlsli`. | P2 |
| `deferred_lighting_ps.hlsl` | `PointLightAttenuation` | Karis 2013 windowed inverse-square falloff — correct | Falcor uses identical windowed falloff | Correct. No change needed. | P3 (no change) |
| `deferred_lighting_ps.hlsl` | Area lights | No area light support (sphere/rect/disc) | Falcor: full LTC (Linearly Transformed Cosines) for rect, disc, sphere analytic area lights | Major gap for interior rendering. Requires `ltc_matrix.hlsl` LUT + Heitz 2016 LTC evaluation. Not a BRDF fix — separate feature. | P3 (separate task) |
| `deferred_lighting_ps.hlsl` | Shading model dispatch | `if (shadingModelID == N)` inline branches for 10 shading models | Falcor: material type enum with `IMaterial::evalBSDF` virtual dispatch (in path tracer), inline branching in rasterization | Inline branching in deferred pass is correct for rasterization — GPU compiles branches as predicated code. No performance issue with 10 models. Architecture is acceptable. | P3 (no change) |
| `mesh_ps.hlsl` | Main shader | Forward stub — `albedo * (NdotL * lightColor + ambientColor)` | N/A — deferred is the real path | Stub is acceptable as a forward translucency fallback. But it uses NO PBR — just lambert. For transparent objects, this needs full PBR evaluation. Not a BRDF improvement per se — a missing feature. | P2 (separate task) |
| `mesh_vs.hlsl` | TBN output | Tangent passed as `float4` (w = handedness) | Same convention in Falcor | Correct. No change needed. | P3 (no change) |

---

## Priority Summary

### P1 — Correctness Bugs (fix immediately)

| ID | Issue |
|----|-------|
| 1 | `D_GGX` — no alpha=0 guard, numerical divergence for mirror surfaces |
| 2 | `Fd_Burley` — missing `/1.51` Frostbite energy conservation factor (over-bright rough diffuse) |
| 3 | `lighting.hlsli` + `light_types.hlsli` — dead stubs, misleading, should be removed or unified with real structs |
| 4 | `gbuffer_ps.hlsl` — inline duplicate of `OctEncode` diverges from `octahedral.hlsli` |

### P2 — Quality Gaps (schedule)

| ID | Issue |
|----|-------|
| 5 | `ImportanceSampleGGX` → replace with VNDF (Heitz 2018) in IBL baker compute |
| 6 | `OctEncode`/`OctDecode` — switch to equal-area octahedral encoding |
| 7 | `V_SmithGGXCorrelated` vs `Vis_SmithJointApprox` — two functions, pick one |
| 8 | `D_Charlie` + `V_Neubelt` — move from `deferred_lighting_ps.hlsl` to `pbr_common.hlsli` |
| 9 | IBL mip selection — replace linear roughness-to-mip with `ComputeEnvLOD` (log2-based) |
| 10 | `math_util.hlsli` — add `M_2PI`, `M_1_PI`, `M_4PI`, PCG hash, `branchlessONB` |
| 11 | `ibl.hlsli` — add `SolidAngleSample`, `ComputeEnvLOD` from spec |
| 12 | `F_Schlick` — add conductor Fresnel `evalFresnelConductor(eta, k, cosTheta)` |
| 13 | `mesh_ps.hlsl` forward path — needs full PBR for translucency, not lambert |
| 14 | `Fd_Burley` double-conversion — `EvaluateBRDF` converts `sqrt(alpha)` back to roughness unnecessarily |

### P3 — Future Improvements (low urgency)

| ID | Issue |
|----|-------|
| 15 | Oren-Nayar diffuse for very rough surfaces |
| 16 | Diffuse multi-scatter compensation (Turquin 2019) |
| 17 | Area lights via LTC (Heitz 2016) — separate task |
| 18 | GBuffer material ID encoding — dedicated integer channel |
| 19 | Anisotropic tangent storage in GBuffer |
| 20 | `pow2/4/5` vectorized overloads |

---

## References

- @ref Heitz 2014 "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs" (JCGT vol3 no2)
- @ref Heitz 2018 "Sampling the GGX Distribution of Visible Normals" (JCGT vol7 no4)
- @ref Lagarde 2014 "Moving Frostbite to Physically Based Rendering 3.0" (SIGGRAPH 2014)
- @ref Burley 2012 "Physically Based Shading at Disney" (SIGGRAPH 2012)
- @ref Karis 2013 "Real Shading in Unreal Engine 4" (SIGGRAPH 2013)
- @ref Cigolle 2014 "A Survey of Efficient Representations for Independent Unit Vectors" (JCGT vol3 no2)
- @ref Duff 2017 "Building an Orthonormal Basis, Revisited" (JCGT vol6 no1)
- @ref Kulla & Conty 2017 "Revisiting Physically Based Shading at Imageworks" (SIGGRAPH 2017)
- @ref Fdez-Aguera 2019 "A Multiple-Scattering Microfacet Model for Real-Time Image Based Lighting" (JCGT vol8 no1)
- @ref Turquin 2019 "Practical multiple scattering compensation for microfacet models" (technical note)
- @ref Estevez & Kulla 2017 "Production Friendly Microfacet Sheen BRDF" (SIGGRAPH 2017)
- @ref Heitz 2016 "Real-Time Polygonal-Light Shading with Linearly Transformed Cosines" (SIGGRAPH 2016)
