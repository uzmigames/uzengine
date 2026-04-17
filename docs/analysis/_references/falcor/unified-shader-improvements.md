<!-- doc-meta: category=analysis, area=falcor, topic=unified-shader-improvements -->

# Unified Shader Improvement Table — UzEngine vs Falcor

> Compiled from 4 deep shader-by-shader analyses covering 100+ shader files.
> Date: 2026-03-30

---

## CRITICAL BUGS (fix immediately — 14 total)

These are correctness bugs that produce wrong output or crash at runtime.

| # | File | Bug | Impact | Source Report |
|---|------|-----|--------|---------------|
| 1 | `compute/draw_compact_cs.hlsl` | Entry point named `CSMain` instead of `main` | Shader silently fails to compile; draw compaction broken | VFX/Compute |
| 2 | `compute/mipgen_cs.hlsl` | Entry point named `CSMain` instead of `main` | Shader silently fails to compile; mipgen broken | VFX/Compute |
| 3 | `vfx/particle_billboard_vs.hlsl` | Sorted index buffer bound but never read — `particleID = vertexID / 6` always | Alpha-blended particles render in wrong order (pool order) | VFX/Compute |
| 4 | `vfx/decal_deferred_ps.hlsl` | Non-replace blend modes use `float3(0.5)` instead of GBuffer albedo | Additive/multiply/screen decal blending visually wrong | VFX/Compute |
| 5 | `vfx/particle_mesh_instance_cs.hlsl` | Missing `GroupMemoryBarrierWithGroupSync` before `InterlockedAdd` | Data race — intermittent wrong instance counts | VFX/Compute |
| 6 | `vfx/vfx_collision.hlsli` | Standoff `* 0.001` without znear/zfar | Camera-distance-dependent collision offset | VFX/Compute |
| 7 | `ibl/equirect_to_cubemap_cs.hlsl` | Produces Y-up cubemap for Z-up engine | IBL sampling may use wrong directions | VFX/Compute |
| 8 | `include/texture_binding.hlsli` | `#else` (SM5) block empty — all macros undefined | SM5 shader compilation fails | VFX/Compute |
| 9 | `include/shadow_debug.hlsli` | All 4 diagnostic functions return hardcoded values | Buffer visualization grid non-functional | Shadows/GI |
| 10 | `include/aerial_perspective.hlsli` | `ComputeAerialPerspective` returns `float4(0,0,0,1)` | No aerial perspective on scene geometry | Shadows/GI |
| 11 | `ssgi/prev_color_reduce_cs.hlsl` | Only mip 0 generated; SSGI reads mips 1-4 | SSGI samples undefined data | Shadows/GI |
| 12 | `include/sdf_common.hlsli` | `LineBoxIntersect` caller passes position instead of direction | DFS cone trace ray bounds wrong | Shadows/GI |
| 13 | `gi/global_sdf_composite_cs.hlsl` | AABB cull uses local-space bounds vs world-space position | Occludes valid meshes, misses others | Shadows/GI |
| 14 | `geometry/gbuffer_ps.hlsl` | Inline duplicate `OctEncode` not using `octahedral.hlsli` | Will diverge on future octahedral updates | PBR/BRDF |

---

## P1 — HIGH PRIORITY (correctness + quality — 18 total)

These significantly affect visual quality or are prerequisites for planned features.

| # | File | Improvement | Current | Target | Reference |
|---|------|-------------|---------|--------|-----------|
| 1 | `include/pbr_common.hlsli` | **D_GGX alpha=0 guard** | No guard — diverges for mirror surfaces | Return 0 when alpha < 1e-3 (delta BSDF) | Falcor `SpecularMicrofacet.slang` |
| 2 | `include/pbr_common.hlsli` | **Fd_Burley energy conservation** | Missing `/1.51` factor | Add Frostbite energy factor: `lerp(1.0, 1/1.51, roughness)` | Lagarde 2015 |
| 3 | `deferred_lighting_ps.hlsl` | **Inconsistent Smith visibility** | Uses `Vis_SmithJointApprox` (different from `pbr_common.hlsli`) | Unify to single `V_SmithGGXCorrelated` | Heitz 2014 |
| 4 | `postprocess/taa_resolve_cs.hlsl` | **Variance clipping** | AABB min/max clamp (ghosting on motion) | Variance clip: μ ± γ·σ | Salvi 2016 |
| 5 | `postprocess/bloom_threshold_cs.hlsl` | **Smooth bloom knee** | Hard threshold → flicker near threshold | Quadratic soft knee | Karis 2013 |
| 6 | `postprocess/bloom_composite_ps.hlsl` | **Pre-tonemap bloom** | Applied post-tonemap | Apply bloom in HDR before tone mapping | Physically correct |
| 7 | `postprocess/smaa_edge_ps.hlsl` | **Complete SMAA** | Only edge pass — passes 2 & 3 missing | Implement blend weight + neighborhood blend | SMAA 2012 |
| 8 | `postprocess/ssr_trace_cs.hlsl` | **Stochastic multi-ray SSR** | Single mirror-direction ray | GGX importance-sampled rays for rough reflections | Stachowiak 2015 |
| 9 | `compute/hiz_pyramid_cs.hlsl` | **Single-pass downsampler** | 11 dispatches for 2048px | AMD SPD — 1 dispatch for full mip chain | AMD 2020 |
| 10 | `ibl/brdf_lut_cs.hlsl` | **Multi-scatter energy compensation** | Missing Ems correction | Add Turquin 2019 fit — rough metals too dark | Turquin 2019 |
| 11 | `ibl/brdf_lut_cs.hlsl` | **VNDF sampling** | NDF sampling (high variance at grazing) | VNDF: 2-4× lower variance | Heitz 2018 |
| 12 | `ssgi/ssgi_trace_cs.hlsl` | **Hi-Z traversal** | Linear screen-space march | Hi-Z hierarchical traversal (catches thin geometry) | McGuire & Mara 2014 |
| 13 | `ssgi/ssgi_temporal_accum_cs.hlsl` | **Variance-guided clipping** | AABB min/max | Variance clip (same as TAA fix) | Schied 2017 (SVGF) |
| 14 | `include/pcss.hlsli` | **Per-pixel Poisson rotation** | Static kernel → visible banding | Rotate disk per-pixel using IGN or blue noise | Fernando 2005 |
| 15 | `include/math_util.hlsli` | **PI constant variants** | Three inline `3.14159265` in lighting shader | Add `M_2PI`, `M_1_PI`, `M_PI_2`, `M_PI_4` | Standard |
| 16 | `include/math_util.hlsli` | **branchlessONB** | Per-shader TBN construction | Pixar branchless ONB (no sqrt, no singularity) | Duff et al. 2017 |
| 17 | `include/math_util.hlsli` | **PCG hash** | Ad-hoc per-shader hashing | Shared `pcgHash(uint)` for per-pixel RNG | O'Neill 2014 |
| 18 | `vfx/vfx_common.hlsli` | **Analytical curl noise** | 6×FBM finite-difference curl (192 gradient dots) | 3×FBM analytical curl (50% savings) | Bridson 2007 |

---

## P2 — MEDIUM PRIORITY (quality polish — 22 total)

| # | File | Improvement | Reference |
|---|------|-------------|-----------|
| 1 | `include/octahedral.hlsli` | Equal-area octahedral encoding (uniform error distribution) | Meyer et al. 2010 |
| 2 | `include/pbr_common.hlsli` | Fresnel conductor (`evalFresnelConductor`) for physical metals | Shirley 1997 |
| 3 | `include/pbr_common.hlsli` | Fresnel dielectric (`evalFresnelDielectric`) for glass/water | PBR Book 2018 |
| 4 | `include/pbr_common.hlsli` | Oren-Nayar diffuse BRDF for rough matte surfaces | Oren & Nayar 1994 |
| 5 | `deferred_lighting_ps.hlsl` | Extract `D_Charlie` + `V_Neubelt` to shared `sheen.hlsli` | Estevez & Kulla 2017 |
| 6 | `include/ibl.hlsli` | Log2-based `ComputeEnvLOD` mip selection (not linear) | Lagarde 2014 |
| 7 | `include/ibl.hlsli` | `SolidAngleSample` for importance-sampled IBL | Karis 2013 |
| 8 | `postprocess/exposure_average_cs.hlsl` | Physical camera model (ISO, f-number, shutter speed → EV100) | Standard photography |
| 9 | `postprocess/gtao_cs.hlsl` | Multi-bounce approximation: `pow(AO, 1+2.5*AO)` | Jimenez 2016 |
| 10 | `postprocess/gtao_cs.hlsl` | Bent normal output for directional GI occlusion | Jimenez 2016 |
| 11 | `postprocess/motion_blur_cs.hlsl` | Tile classification pre-pass (skip static regions) | McGuire et al. 2012 |
| 12 | `postprocess/velocity_ps.hlsl` | Per-vertex motion vectors for skinned meshes | Standard |
| 13 | `postprocess/dof_blur_cs.hlsl` | Recombine pass for near/far/sharp layer separation | Lee & Eisemann 2013 |
| 14 | `postprocess/color_grading_lut_cs.hlsl` | External artist LUT blending + HDR output path | Selan 2012 |
| 15 | `ibl/prefilter_cs.hlsl` | Mip-bias for firefly suppression in rough reflections | Lagarde 2014 |
| 16 | `compute/mipgen_cs.hlsl` | Kaiser filter + renormalized-average for normal maps | Falcor pattern |
| 17 | `compute/gpu_cull_cs.hlsl` | `groupshared` frustum planes (extracted once, not 64×) | Wihlidal 2015 |
| 18 | `compute/gpu_cull_cs.hlsl` | `WavePrefixCountBits` for slot allocation (1 atomic/wave) | SM6.0 wave intrinsics |
| 19 | `vfx/particle_billboard_ps.hlsl` | Particle lighting (sample ambient SH / irradiance) | Standard |
| 20 | `vfx/particle_billboard_ps.hlsl` | Separate additive blend pass for emissive particles | Standard |
| 21 | `compute/decal_apply_cs.hlsl` | World-space normal blending (decode→blend→re-encode) | Correct |
| 22 | `vfx/vfx_motion_vectors.hlsli` | Store true `prevPosition` per-particle (not backward Euler) | Falcor pattern |

---

## P3 — LOW PRIORITY / LONG-TERM (15 total)

| # | Area | Improvement | Reference |
|---|------|-------------|-----------|
| 1 | Materials | Anisotropic GGX NDF + tangent in GBuffer | Walter 2007 (aniso extension) |
| 2 | Materials | Hair BSDF (Chiang 2016 multi-lobe) | Chiang et al. 2016 |
| 3 | Materials | Layered BSDFs (coat + base) | Falcor `LayeredBSDF.slang` |
| 4 | Materials | Diffuse transmission (thin geometry) | Falcor pattern |
| 5 | Materials | Material type ID in GBuffer for multi-BSDF dispatch | Falcor architecture |
| 6 | Lighting | LTC area lights (rect/disc/sphere) | Heitz et al. 2016 |
| 7 | Lighting | Light BVH for emissive triangles | Moreau 2019 |
| 8 | Lighting | Environment map MIS-weighted importance sampling | Falcor `EnvMapSampler` |
| 9 | Tone mapping | Selectable operators (Reinhard, Hable UC2) | Falcor `ToneMapper` |
| 10 | Tone mapping | White balance (Kelvin → RGB) | Standard |
| 11 | Post-FX | Vignette + chromatic aberration | Falcor `SimplePostFX` |
| 12 | Clouds | Detail noise for erosion | Schneider & Vos 2015 |
| 13 | Clouds | Temporal reprojection (half-res → full-res) | Standard |
| 14 | God rays | 3-pass progressive blur (not single 64-sample) | Volumetric shafts |
| 15 | Testing | FLIP perceptual quality metric pass | Andersson 2020 |

---

## Summary Statistics

| Category | Count |
|----------|-------|
| Critical bugs | 14 |
| P1 — High priority | 18 |
| P2 — Medium priority | 22 |
| P3 — Low priority | 15 |
| **Total improvements** | **69** |

### By subsystem

| Subsystem | Critical | P1 | P2 | P3 | Total |
|-----------|----------|----|----|-------|-------|
| PBR / BRDF / Materials | 1 | 3 | 5 | 5 | 14 |
| Post-Processing | 0 | 5 | 6 | 3 | 14 |
| Shadows / GI / Atmosphere | 4 | 3 | 0 | 3 | 10 |
| VFX / Particles | 4 | 1 | 3 | 0 | 8 |
| Compute / Utility | 2 | 3 | 3 | 0 | 8 |
| IBL | 1 | 2 | 2 | 1 | 6 |
| Math / Shared Includes | 2 | 3 | 1 | 0 | 6 |
| Lighting | 0 | 0 | 0 | 3 | 3 |
| **Total** | **14** | **18** | **22** | **15** | **69** |

---

## Detailed Reports

| Report | File |
|--------|------|
| PBR / BRDF / Lighting | [shader-improvement-table-pbr.md](shader-improvement-table-pbr.md) |
| Post-Processing / AA / Tone Mapping | [shader-improvement-table-postfx.md](shader-improvement-table-postfx.md) |
| Shadows / GI / Atmosphere | [shader-improvement-table-shadows-gi-atmo.md](shader-improvement-table-shadows-gi-atmo.md) |
| VFX / Compute / IBL | [shader-improvement-table-vfx-compute.md](shader-improvement-table-vfx-compute.md) |
