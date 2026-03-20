<!-- doc-meta
type: reference
category: rendering
title: UzEngine Academic & Industry References
created: 2026-03-20
updated: 2026-03-20
status: active
-->

# UzEngine — Academic & Industry References

Complete bibliography of published papers, standards, and techniques referenced
throughout the UzEngine rendering pipeline. Each entry lists the original
publication, authors, where it is used in the engine, and a URL when available.

---

## Table of Contents

1. [PBR & BRDF](#1-pbr--brdf)
2. [Tone Mapping & Color Science](#2-tone-mapping--color-science)
3. [Atmospheric Scattering & Sky](#3-atmospheric-scattering--sky)
4. [Fog & Volumetric Rendering](#4-fog--volumetric-rendering)
5. [Ambient Occlusion](#5-ambient-occlusion)
6. [Screen-Space Reflections](#6-screen-space-reflections)
7. [Screen-Space Global Illumination](#7-screen-space-global-illumination)
8. [Global Illumination & Radiance Caching](#8-global-illumination--radiance-caching)
9. [Signed Distance Fields](#9-signed-distance-fields)
10. [Shadow Mapping](#10-shadow-mapping)
11. [Temporal Anti-Aliasing & Upscaling](#11-temporal-anti-aliasing--upscaling)
12. [Bloom & Post-Processing](#12-bloom--post-processing)
13. [Depth of Field](#13-depth-of-field)
14. [Motion Blur](#14-motion-blur)
15. [Parallax Occlusion Mapping](#15-parallax-occlusion-mapping)
16. [GPU Culling & Visibility](#16-gpu-culling--visibility)
17. [Sampling & Noise](#17-sampling--noise)
18. [Spherical Harmonics & IBL](#18-spherical-harmonics--ibl)
19. [Denoising & Filtering](#19-denoising--filtering)
20. [Sharpening](#20-sharpening)
21. [Volumetric Clouds](#21-volumetric-clouds)
22. [God Rays](#22-god-rays)
23. [Geometry & Math](#23-geometry--math)
24. [Standards & Specifications](#24-standards--specifications)

---

## 1. PBR & BRDF

### Burley 2012 — Disney Diffuse BRDF
- **Title**: "Physically Based Shading at Disney"
- **Authors**: Brent Burley
- **Venue**: SIGGRAPH 2012 Course: Practical Physically Based Shading in Film and Game Production
- **URL**: https://blog.selfshadow.com/publications/s2012-shading-course/burley/s2012_pbs_disney_brdf_notes_v3.pdf
- **Used in**: `pbr_common.hlsli` (DiffuseDisneyBurley), `lighting.hlsli`, `deferred_lighting_ps.hlsl`, `brdf_math.h`
- **Key contribution**: Roughness-dependent retroreflective diffuse model replacing Lambertian. FD90 = 0.5 + 2·VoH²·roughness.

### Heitz 2014 — Smith Joint Visibility
- **Title**: "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
- **Authors**: Eric Heitz
- **Venue**: Journal of Computer Graphics Techniques, Vol. 3, No. 2, 2014
- **URL**: https://jcgt.org/published/0003/02/03/
- **Used in**: `pbr_common.hlsli` (VisibilitySmithGGX), `lighting.hlsli`, `brdf_math.h`
- **Key contribution**: Height-correlated Smith geometry/visibility term approximation for GGX. Vis = G/(4·NoV·NoL).

### Walter et al. 2007 — GGX Microfacet NDF
- **Title**: "Microfacet Models for Refraction through Rough Surfaces"
- **Authors**: Bruce Walter, Stephen R. Marschner, Hongsong Li, Kenneth E. Torrance
- **Venue**: EGSR 2007
- **URL**: https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.pdf
- **Used in**: `pbr_common.hlsli` (DistributionGGX)
- **Key contribution**: GGX/Trowbridge-Reitz normal distribution function. D(h) = α²/(π·((N·H)²·(α²-1)+1)²).

### Karis 2013 — Real Shading
- **Title**: "Real Shading in Unreal Engine 4"
- **Authors**: Brian Karis
- **Venue**: SIGGRAPH 2013 Course: Physically Based Shading in Theory and Practice
- **URL**: https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
- **Used in**: `pbr_common.hlsli` (EvaluateBRDF), `bloom_downsample_cs.hlsl` (Karis average), `deferred_lighting_ps.hlsl`
- **Key contribution**: Cook-Torrance BRDF assembly (D_GGX × Vis_Smith × F_Schlick), importance-sampled split-sum IBL, Karis average for bloom anti-flicker.

### Schlick 1994 — Fresnel Approximation
- **Title**: "An Inexpensive BRDF Model for Physically-Based Rendering"
- **Authors**: Christophe Schlick
- **Venue**: Computer Graphics Forum, Vol. 13, No. 3, 1994
- **URL**: https://doi.org/10.1111/1467-8659.1330233
- **Used in**: `pbr_common.hlsli` (FresnelSchlick), `ssr_resolve_cs.hlsl`
- **Key contribution**: F(θ) = F0 + (1-F0)·(1-cos θ)⁵ — fast Fresnel approximation.

### Turquin 2018 — Multiscatter Energy Compensation
- **Title**: "Practical Multiple Scattering Compensation for Microfacet Models"
- **Authors**: Emmanuel Turquin
- **Venue**: 2018 (white paper)
- **URL**: https://blog.selfshadow.com/publications/turquin/ms_comp_final.pdf
- **Used in**: `pbr_common.hlsli` (EnergyAlbedoFitGGX, MultiscatterCompensation), `brdf_math.h`
- **Key contribution**: Analytic fit for single-scatter directional albedo + Kulla-Conty energy compensation weight W.

### Kulla & Conty 2017 — Multiscatter BRDF
- **Title**: "Revisiting Physically Based Shading at Imageworks"
- **Authors**: Christopher Kulla, Alejandro Conty
- **Venue**: SIGGRAPH 2017 Course
- **URL**: https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_slides_v2.pdf
- **Used in**: `pbr_common.hlsli` (MultiscatterCompensation)
- **Key contribution**: Multi-bounce energy compensation framework for microfacet BRDFs.

### Lagarde 2014 — Frostbite PBR / F90 Micro-Occlusion
- **Title**: "Moving Frostbite to Physically Based Rendering"
- **Authors**: Sébastien Lagarde, Charles de Rousiers
- **Venue**: SIGGRAPH 2014 Course
- **URL**: https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
- **Used in**: `pbr_common.hlsli` (F90 micro-occlusion term)
- **Key contribution**: F90 = saturate(50·SpecularColor.g) for micro-occlusion at grazing angles.

---

## 2. Tone Mapping & Color Science

### ACES Reference Implementation (Academy S-2014-006)
- **Title**: Academy Color Encoding System — Reference Implementation
- **Authors**: Academy of Motion Picture Arts and Sciences
- **Venue**: ACES 1.0 (2014), updated through ACES 1.3
- **URL**: https://github.com/ampas/aces-dev
- **Used in**: `film_tonemap.hlsli` (ApplyToneCurve, RgbToSaturation, RgbToHue, CenterHue, RgbToLumaChroma, SigmoidShaper, GlowModuleFwd), `color_grading_lut_cs.hlsl` (BradfordAdapt, ApplyWhiteBalance), `brdf_math.h`
- **Key contribution**: Complete RRT+ODT pipeline: AP0/AP1 color spaces, glow module, red modifier, 3-segment S-curve, chromatic adaptation (Bradford).

### Narkowicz 2015 — ACES Filmic Curve Fit
- **Title**: "ACES Filmic Tone Mapping Curve"
- **Authors**: Krzysztof Narkowicz
- **Venue**: Blog post, 2015
- **URL**: https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
- **Used in**: `pbr_common.hlsli` (ACESFilm — simplified single-function approximation)
- **Key contribution**: Polynomial fit: (x·(2.51x+0.03))/(x·(2.43x+0.59)+0.14).

### Selan 2012 — Color Grading LUT
- **Title**: "Using Lookup Tables to Accelerate Color Transformations"
- **Authors**: Jeremy Selan (Sony Pictures Imageworks)
- **Venue**: GPU Gems 2 / GPU Pro, 2012
- **Used in**: `color_grading_lut_cs.hlsl` (full CombineLUTs pipeline: white balance, color correction, gamut mapping, tone curve bake)
- **Key contribution**: 32³ 3D LUT baking pipeline for real-time color grading.

### Krystek 1985 / CIE Colorimetry — D-Illuminant Chromaticity
- **Title**: CIE Standard Illuminant D Series
- **Authors**: CIE (Krystek 1985 approximation)
- **Used in**: `color_grading_lut_cs.hlsl` (ApplyWhiteBalance — D-illuminant xy from color temperature)
- **Key contribution**: Planckian locus chromaticity approximation for white balance by color temperature.

### Reinhard et al. 2002 — Auto-Exposure
- **Title**: "Photographic Tone Reproduction for Digital Images"
- **Authors**: Erik Reinhard, Michael Stark, Peter Shirley, James Ferwerda
- **Venue**: SIGGRAPH 2002
- **URL**: https://doi.org/10.1145/566570.566575
- **Used in**: `exposure_average_cs.hlsl` (histogram-based auto-exposure)
- **Key contribution**: Key value / log-average luminance exposure model.

---

## 3. Atmospheric Scattering & Sky

### Bruneton & Neyret 2008 — Precomputed Atmospheric Scattering
- **Title**: "Precomputed Atmospheric Scattering"
- **Authors**: Eric Bruneton, Fabrice Neyret
- **Venue**: EGSR 2008
- **URL**: https://hal.inria.fr/inria-00288758/document
- **Used in**: `transmittance_lut_cs.hlsl`, `multi_scatter_lut_cs.hlsl`, `sky_atmosphere_ps.hlsl`, `aerial_perspective_volume_cs.hlsl`, `units.hlsli`, atmosphere renderer
- **Key contribution**: Precomputed transmittance and scattering LUTs for real-time atmosphere rendering. Earth defaults: bottom radius 6360 km, atmosphere height 60 km, Rayleigh scale height 8 km.

### Hillaire 2020 — Scalable Sky & Atmosphere
- **Title**: "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"
- **Authors**: Sébastien Hillaire
- **Venue**: EGSR 2020
- **URL**: https://sebh.github.io/publications/egsr2020.pdf
- **Used in**: `multi_scatter_lut_cs.hlsl` (geometric series multi-scatter), `aerial_perspective_volume_cs.hlsl`, `sky_atmosphere_ps.hlsl`
- **Key contribution**: Multi-scatter LUT (geometric series, 64 dirs × 20 steps), aerial perspective 3D volume texture, ozone tent distribution.

### Hillaire 2016 — Frostbite Volumetric Rendering
- **Title**: "Physically Based and Unified Volumetric Rendering in Frostbite"
- **Authors**: Sébastien Hillaire
- **Venue**: SIGGRAPH 2016 Course
- **URL**: https://www.ea.com/frostbite/news/physically-based-unified-volumetric-rendering-in-frostbite
- **Used in**: `height_fog_ps.hlsl` (IntegrateExponentialFogRay), `fog_renderer.cpp`, `fog_renderer.h`
- **Key contribution**: Analytical exponential height fog integration with directional inscattering, sky-to-fog color injection.

### Rayleigh 1871 — Rayleigh Scattering Phase Function
- **Authors**: Lord Rayleigh
- **Used in**: `aerial_perspective_volume_cs.hlsl`, `transmittance_lut_cs.hlsl`
- **Key contribution**: Phase(θ) = 3/(16π)·(1+cos²θ).

### Henyey & Greenstein 1941 — Mie Phase Function
- **Title**: "Diffuse Radiation in the Galaxy"
- **Authors**: L.G. Henyey, J.L. Greenstein
- **Venue**: Astrophysical Journal, 1941
- **Used in**: `aerial_perspective_volume_cs.hlsl`, atmosphere shaders
- **Key contribution**: HG phase function: (1-g²)/(4π·(1+g²-2g·cosθ)^1.5).

---

## 4. Fog & Volumetric Rendering

See [Hillaire 2016](#hillaire-2016--frostbite-volumetric-rendering) above.

---

## 5. Ambient Occlusion

### Jimenez 2016 — GTAO
- **Title**: "Practical Realtime Strategies for Accurate Indirect Occlusion"
- **Authors**: Jorge Jimenez, Xian-Chun Wu, Angelo Pesce, Adrian Jarabo
- **Venue**: SIGGRAPH 2016
- **URL**: https://www.activision.com/cdn/research/Practical_Realtime_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
- **Used in**: `gtao_cs.hlsl` (FindMaxHorizonAngle, IntegrateHorizonArc), `gtao_temporal_cs.hlsl`, `gtao_denoise_cs.hlsl`, `gtao_upsample_ps.hlsl`
- **Key contribution**: Ground-Truth Ambient Occlusion — dual-direction horizon search with cosine-weighted inner integral, thickness heuristic, multi-bounce colored AO.

### Penner 2011 — Fast ArcCos
- **Title**: "Pre-Integrated Skin Shading" (includes fast math utilities)
- **Authors**: Eric Penner
- **Venue**: GPU Pro 2, 2011
- **Used in**: `gtao_cs.hlsl` (fast acos approximation)
- **Key contribution**: Fast polynomial acos: acosFast4(x) ≈ (-0.0187·x²+0.0742)·x²-0.2121)·x²+1.5707963) · √(1-x).

---

## 6. Screen-Space Reflections

### Stachowiak 2015 — Stochastic SSR
- **Title**: "Stochastic Screen-Space Reflections"
- **Authors**: Tomasz Stachowiak
- **Venue**: SIGGRAPH 2015 Advances in Real-Time Rendering
- **URL**: https://www.ea.com/frostbite/news/stochastic-screen-space-reflections
- **Used in**: `ssr_trace_cs.hlsl` (hierarchical ray march), `ssr_resolve_cs.hlsl`, `prev_color_reduce_cs.hlsl`
- **Key contribution**: Hi-Z hierarchical screen-space ray tracing with stochastic roughness sampling.

### McGuire & Mara 2014 — Efficient GPU SSR
- **Title**: "Efficient GPU Screen-Space Ray Tracing"
- **Authors**: Morgan McGuire, Michael Mara
- **Venue**: JCGT, Vol. 3, No. 4, 2014
- **URL**: https://jcgt.org/published/0003/04/04/
- **Used in**: `ssr_trace_cs.hlsl`, `ssr_resolve_cs.hlsl`, `ssgi_trace_cs.hlsl`
- **Key contribution**: DDA-based screen-space ray marching with perspective-correct stepping.

---

## 7. Screen-Space Global Illumination

### Jimenez 2016 — SSGI / Screen-Space Indirect Lighting
- See [Jimenez 2016 — GTAO](#jimenez-2016--gtao) (same paper covers both GTAO and SSGI).
- **Used in**: `ssgi_trace_cs.hlsl`, `ssgi_upsample_cs.hlsl`

---

## 8. Global Illumination & Radiance Caching

### Majercik et al. 2019 — DDGI
- **Title**: "Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Fields"
- **Authors**: Zander Majercik, Jean-Philippe Guertin, Derek Nowrouzezahrai, Morgan McGuire
- **Venue**: I3D 2019
- **URL**: https://morgan3d.github.io/articles/2019-04-01-ddgi/
- **Used in**: `radiance_cache_trace_cs.hlsl`, `radiance_cache_common.hlsli`, `radiance_cache.h/cpp`
- **Key contribution**: Probe-based irradiance field with exponential weighting, SH storage, real-time update.

### Wright 2021 — Radiance Caching
- **Title**: "Radiance Caching for Real-Time Global Illumination"
- **Authors**: Daniel Wright
- **Venue**: SIGGRAPH 2021 Advances in Real-Time Rendering
- **Used in**: `radiance_cache_trace_cs.hlsl`
- **Key contribution**: Screen-space radiance cache with SDF-guided ray tracing.

### Ramamoorthi & Hanrahan 2001 — SH Irradiance
- **Title**: "An Efficient Representation for Irradiance Environment Maps"
- **Authors**: Ravi Ramamoorthi, Pat Hanrahan
- **Venue**: SIGGRAPH 2001
- **URL**: https://doi.org/10.1145/383259.383317
- **Used in**: `sh_irradiance.hlsli`, `sh_projection.h/cpp`, `radiance_cache_common.hlsli`
- **Key contribution**: L2 spherical harmonics (9 coefficients) sufficient for diffuse irradiance. Cosine-lobe convolution in SH domain.

### Keinert et al. 2015 — Spherical Fibonacci
- **Title**: "Improved Spherical Fibonacci Point Sets for Computer Graphics"
- **Authors**: Benjamin Keinert et al.
- **Venue**: 2015
- **Used in**: `radiance_cache_trace_cs.hlsl` (probe ray directions)
- **Key contribution**: Uniform hemisphere sampling via Fibonacci spiral for probe tracing.

---

## 9. Signed Distance Fields

### Wright 2015 — Distance Field Shadows & Occlusion
- **Title**: "Dynamic Occlusion with Signed Distance Fields"
- **Authors**: Daniel Wright
- **Venue**: SIGGRAPH 2015 Advances in Real-Time Rendering
- **URL**: https://advances.realtimerendering.com/s2015/
- **Used in**: `dfs_cone_trace_cs.hlsl`, `dfs_cull_objects_cs.hlsl`, `dfs_upsample_cs.hlsl`, `dfs_common.hlsli`, `sdf_common.hlsli`, `global_sdf_composite_cs.hlsl`, `mesh_sdf_bake_cs.hlsl`, SDF atlas manager, shadow renderer
- **Key contribution**: Per-mesh SDF baking, global distance field cascade, cone-trace soft shadows from SDFs.

### Hart 1996 — Sphere Tracing
- **Title**: "Sphere Tracing: A Geometric Method for the Antialiased Ray Tracing of Implicit Surfaces"
- **Authors**: John C. Hart
- **Venue**: The Visual Computer, 1996
- **Used in**: `dfs_cone_trace_cs.hlsl`, `sdf_common.hlsli`, `global_sdf_composite_cs.hlsl`
- **Key contribution**: Ray marching with SDF-guided step sizes for implicit surface intersection.

---

## 10. Shadow Mapping

### Engel 2006 — Cascaded Shadow Maps
- **Title**: "Cascaded Shadow Maps"
- **Authors**: Wolfgang Engel
- **Venue**: ShaderX5, 2006
- **URL**: https://developer.download.nvidia.com/SDK/10.5/opengl/src/cascaded_shadow_maps/doc/cascaded_shadow_maps.pdf
- **Used in**: `shadow_common.hlsli`, `shadow_cascade.h/cpp`, `shadow_cache.h/cpp`, `shadow_renderer.cpp`, `frame_shadows.cpp`
- **Key contribution**: Cascaded/PSSM shadow maps with per-cascade split distances, transition blending.

### Fernando 2005 — PCSS
- **Title**: "Percentage-Closer Soft Shadows"
- **Authors**: Randima Fernando
- **Venue**: SIGGRAPH 2005 / GPU Gems (NVIDIA)
- **URL**: https://developer.download.nvidia.com/shaderlibrary/docs/shadow_PCSS.pdf
- **Used in**: `pcss.hlsli`, `shadow_renderer.cpp`
- **Key contribution**: Blocker search + variable penumbra PCF for physically-based soft shadows.

### Reynolds 2017 — Square/Disc Mapping (Poisson Disk)
- **Title**: "A Low-Distortion Map Between Square and Disk"
- **Authors**: Chad Reynolds
- **Used in**: `pcss.hlsli` (Poisson-like sample distribution for PCF)
- **Key contribution**: Concentric square-to-disk mapping for uniform PCF sample distribution.

### Sobol 1967 — Low-Discrepancy Sequences
- **Title**: "Distribution of Points in a Cube and Approximate Evaluation of Integrals"
- **Authors**: I.M. Sobol
- **Used in**: `pcss.hlsli` (jittered sample rotation)
- **Key contribution**: Quasi-random sampling for shadow filter noise reduction.

---

## 11. Temporal Anti-Aliasing & Upscaling

### Karis 2014 — High Quality Temporal Supersampling
- **Title**: "High Quality Temporal Supersampling"
- **Authors**: Brian Karis
- **Venue**: SIGGRAPH 2014 Advances in Real-Time Rendering
- **URL**: https://advances.realtimerendering.com/s2014/
- **Used in**: `taa_resolve_cs.hlsl`, `tsr_resolve_cs.hlsl`, `prev_color_reduce_cs.hlsl`
- **Key contribution**: YCoCg neighborhood clamping, Catmull-Rom history sampling, velocity-adaptive blend factor, firefly suppression via Karis weight.

### Salvi 2016 — Temporal Supersampling
- **Title**: "An Excursion in Temporal Supersampling"
- **Authors**: Marco Salvi
- **Venue**: GDC 2016
- **Used in**: `gtao_temporal_cs.hlsl`, `ssgi_temporal_accum_cs.hlsl`
- **Key contribution**: Variance-based neighborhood clamping, per-pixel blend factor adaptation.

### Yang et al. 2020 — TSR Survey
- **Title**: "A Survey on Temporal Antialiasing Techniques"
- **Authors**: Lei Yang, Shiqiu Liu, Marco Salvi
- **Venue**: Computer Graphics Forum, 2020
- **Used in**: `tsr_resolve_cs.hlsl`, TSR upscaler
- **Key contribution**: Survey of temporal upscaling techniques, responsive accumulation patterns.

### Wihlidal 2019 — Temporal Super Resolution
- **Title**: "Temporal Super Resolution" (Frostbite)
- **Authors**: Thomas Wihlidal
- **Used in**: `tsr_resolve_cs.hlsl`

### Catmull & Rom 1974 — Spline Interpolation
- **Title**: "A Class of Local Interpolating Splines"
- **Authors**: Edwin Catmull, Raphael Rom
- **Used in**: `taa_resolve_cs.hlsl`, `tsr_resolve_cs.hlsl` (Catmull-Rom 5-tap history filter)

### Halton 1964 — Quasi-Random Sequences
- **Title**: "Radical-Inverse Based Quasi-Random Point Sequences"
- **Authors**: J.H. Halton
- **Venue**: Numerische Mathematik, 1964
- **Used in**: `halton.h` (Halton(2,3) jitter for TAA/TSR)
- **Key contribution**: Low-discrepancy 2D jitter sequence for sub-pixel temporal sampling.

---

## 12. Bloom & Post-Processing

### Jimenez 2014 — Next Gen Post-Processing
- **Title**: "Next Generation Post Processing in Call of Duty: Advanced Warfare"
- **Authors**: Jorge Jimenez
- **Venue**: SIGGRAPH 2014 Advances in Real-Time Rendering
- **URL**: https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare
- **Used in**: `bloom_downsample_cs.hlsl` (13-tap downsample), `dof_blur_cs.hlsl`, various shaders (Interleaved Gradient Noise)
- **Key contribution**: Stable 13-tap downsample pattern for bloom, interleaved gradient noise (IGN) function: frac(52.9829189·frac(dot(pos, float2(0.06711056, 0.00583715)))).

---

## 13. Depth of Field

### Lee & Eisemann 2013 — Real-Time DoF
- **Title**: "Practical Real-Time Lens-Flare Rendering and Depth of Field"
- **Authors**: Sungkil Lee, Elmar Eisemann
- **Venue**: GPU Pro 4, 2013
- **Used in**: `dof_blur_cs.hlsl`, `dof_setup_cs.hlsl`
- **Key contribution**: Gather-based bokeh blur with disc sample ring pattern, CoC-based near/far separation.

---

## 14. Motion Blur

### McGuire et al. 2012 — Reconstruction Motion Blur
- **Title**: "A Reconstruction Filter for Plausible Motion Blur"
- **Authors**: Morgan McGuire, Padraic Hennessy, Michael Bukowski, Brian Osman
- **Venue**: I3D 2012
- **URL**: https://casual-effects.com/research/McGuire2012Blur/
- **Used in**: `motion_blur_cs.hlsl`, `motionblur_tilemax_cs.hlsl`, `motionblur_neighbormax_cs.hlsl`
- **Key contribution**: Tile-based velocity max (TileMax → NeighborMax), depth-aware gather with soft-depth comparison, aspect-corrected blur vectors.

---

## 15. Parallax Occlusion Mapping

### Tatarchuk 2006 — Practical POM
- **Title**: "Practical Parallax Occlusion Mapping for Highly Detailed Surface Rendering"
- **Authors**: Natalya Tatarchuk
- **Venue**: ShaderX5, 2006
- **URL**: https://developer.amd.com/wordpress/media/2012/10/Tatarchuk-POM.pdf
- **Used in**: `pom.hlsli`, `gbuffer_ps.hlsl`, `mesh_ps.hlsl`
- **Key contribution**: Steep parallax + linear interpolation refinement (8-32 layers), self-shadowing ray march.

### Kaneko et al. 2001 — Parallax Mapping Foundation
- **Title**: "Detailed Shape Representation with Parallax Mapping"
- **Authors**: Tomomichi Kaneko et al.
- **Venue**: ICAT 2001
- **Used in**: `pom.hlsli` (foundational parallax offset concept)

### Policarpo et al. 2005 — Relief Mapping
- **Title**: "Real-Time Relief Mapping on Arbitrary Polygonal Surfaces"
- **Authors**: Fabio Policarpo, Manuel M. Oliveira, João Luiz Dihl Comba
- **Venue**: I3D 2005
- **Used in**: `pom.hlsli` (binary search refinement step)

---

## 16. GPU Culling & Visibility

### Wihlidal 2015 — GPU-Driven Rendering
- **Title**: "Optimizing the Graphics Pipeline with Compute"
- **Authors**: Thomas Wihlidal
- **Venue**: GDC 2015
- **URL**: https://www.wihlidal.com/Presentations/GDC2015-Optimizing-the-Graphics-Pipeline-with-Compute.pdf
- **Used in**: `gpu_cull_cs.hlsl`, `hiz_pyramid_cs.hlsl`, GPU cull pass
- **Key contribution**: Two-phase GPU occlusion culling (frustum + HZB), hierarchical Z-buffer construction via compute.

### Greene et al. 1993 — Hierarchical Z-Buffer
- **Title**: "Hierarchical Z-Buffer Visibility"
- **Authors**: Ned Greene, Michael Kass, Gavin Miller
- **Venue**: SIGGRAPH 1993
- **Used in**: `gpu_cull_cs.hlsl`, `hiz_pyramid_cs.hlsl`
- **Key contribution**: Mip-chain depth pyramid for conservative occlusion testing.

### Gribb & Hartmann 2001 — Frustum Plane Extraction
- **Title**: "Fast Extraction of Viewing Frustum Planes from the WorldView-Projection Matrix"
- **Authors**: Gil Gribb, Klaus Hartmann
- **Used in**: `gpu_cull_cs.hlsl`, `dfs_cull_objects_cs.hlsl`
- **Key contribution**: Extract 6 frustum planes directly from VP matrix rows.

---

## 17. Sampling & Noise

### Hammersley 1964 — Monte Carlo Methods
- **Title**: "Monte Carlo Methods"
- **Authors**: J.M. Hammersley, D.C. Handscomb
- **Venue**: Methuen & Co., 1964
- **Used in**: `ssgi_trace_cs.hlsl` (Hammersley low-discrepancy sampling)

### O'Neill 2014 — PCG Random
- **Title**: "PCG: A Family of Simple Fast Space-Efficient Statistically Good Algorithms for Random Number Generation"
- **Authors**: Melissa E. O'Neill
- **Venue**: Harvey Mudd College, 2014
- **URL**: https://www.pcg-random.org/paper.html
- **Used in**: `ssgi_trace_cs.hlsl`, cloud noise shaders (Rand3DPCG16)

### Shirley & Chiu 1997 — Concentric Disk Mapping
- **Title**: "A Low Distortion Map Between Disk and Square"
- **Authors**: Peter Shirley, Kenneth Chiu
- **Venue**: Journal of Graphics Tools, 1997
- **Used in**: `ssgi_trace_cs.hlsl` (CosineSampleHemisphereConcentric)

### Frisvad 2012 — Orthonormal Basis from Normal
- **Title**: "Building an Orthonormal Basis from a 3D Unit Vector Without Normalization"
- **Authors**: Jeppe Revall Frisvad
- **Venue**: Journal of Graphics Tools, 2012
- **Used in**: `ssgi_trace_cs.hlsl` (GetTangentBasis)

### Mitchell 1991 — Poisson Disk Sampling
- **Title**: "Spectrally Optimal Sampling for Distribution Ray Tracing"
- **Authors**: Don P. Mitchell
- **Venue**: SIGGRAPH 1991
- **Used in**: `ssgi_spatial_denoise_cs.hlsl` (Poisson-disk sample offsets)

---

## 18. Spherical Harmonics & IBL

See [Ramamoorthi & Hanrahan 2001](#ramamoorthi--hanrahan-2001--sh-irradiance) in Section 8.

---

## 19. Denoising & Filtering

### Schied et al. 2017 — SVGF
- **Title**: "Spatiotemporal Variance-Guided Filtering: Real-Time Reconstruction for Path-Traced Global Illumination"
- **Authors**: Christoph Schied, Anton Kaplanyan, Chris Wyman, Anjul Patney, Chakravarty R. Alla Chaitanya, John Googley, Aaron Lefohn
- **Venue**: HPG 2017
- **URL**: https://research.nvidia.com/publication/2017-07_spatiotemporal-variance-guided-filtering-real-time-reconstruction-path-traced
- **Used in**: `ssgi_spatial_denoise_cs.hlsl`, `ssgi_temporal_accum_cs.hlsl`
- **Key contribution**: Edge-stopping spatial filter with variance-guided kernel, temporal accumulation with neighborhood clamping.

### Tomasi & Manduchi 1998 — Bilateral Filtering
- **Title**: "Bilateral Filtering for Gray and Color Images"
- **Authors**: Carlo Tomasi, Roberto Manduchi
- **Venue**: ICCV 1998
- **Used in**: `ssgi_spatial_denoise_cs.hlsl`, `ssgi_upsample_cs.hlsl`, `dfs_upsample_cs.hlsl`
- **Key contribution**: Edge-preserving filter combining spatial and range (depth/normal) weighting.

---

## 20. Sharpening

### AMD 2019 — FidelityFX CAS
- **Title**: "FidelityFX Contrast Adaptive Sharpening"
- **Authors**: AMD GPUOpen
- **Venue**: 2019
- **URL**: https://gpuopen.com/fidelityfx-cas/
- **Used in**: `cas_sharpen_ps.hlsl`
- **Key contribution**: Contrast-adaptive sharpening that avoids ringing artifacts.

---

## 21. Volumetric Clouds

### Schneider & Vos 2015 — Real-Time Volumetric Cloudscapes
- **Title**: "The Real-time Volumetric Cloudscapes of Horizon: Zero Dawn"
- **Authors**: Andrew Schneider, Nathan Vos
- **Venue**: SIGGRAPH 2015 Advances in Real-Time Rendering
- **URL**: https://advances.realtimerendering.com/s2015/
- **Used in**: `cloud_raymarch_cs.hlsl`, `cloud_noise_shape_cs.hlsl`, `cloud_noise_detail_cs.hlsl`, `cloud_temporal_cs.hlsl`
- **Key contribution**: Worley+Perlin 3D noise for cloud shape, ray-march with Beer-Lambert extinction, temporal reprojection.

---

## 22. God Rays

### Volumetric Light Shafts (Radial Blur)
- **Used in**: `god_rays_cs.hlsl`
- **Technique**: Radial blur from sun screen position with occlusion mask, multi-pass decay accumulation.

---

## 23. Geometry & Math

### Ericson 2005 — Real-Time Collision Detection
- **Title**: "Real-Time Collision Detection"
- **Authors**: Christer Ericson
- **Venue**: Morgan Kaufmann, 2005
- **Used in**: `gpu_cull_cs.hlsl` (sphere-frustum, AABB-frustum tests)

### Williams et al. 2005 — Ray-AABB Intersection
- **Title**: "An Efficient and Robust Ray-Box Intersection Algorithm"
- **Authors**: Amy Williams, Steve Barrus, R. Keith Morley, Peter Shirley
- **Venue**: JCGT, 2005
- **Used in**: `sdf_common.hlsli`, `dfs_cone_trace_cs.hlsl` (slab intersection)

---

## 24. Standards & Specifications

### IEC 61966-2-1:1999 — sRGB
- **Title**: "Multimedia systems and equipment — Colour measurement and management"
- **Used in**: `math_util.hlsli` (LinearToSRGB, SRGBToLinear — piecewise transfer function)

### ITU-R BT.709 — HD Video Luminance
- **Title**: "Parameter values for the HDTV standards for production and international programme exchange"
- **Used in**: `ssgi_temporal_accum_cs.hlsl`, `prev_color_reduce_cs.hlsl` (Rec.709 luminance weights: 0.2126, 0.7152, 0.0722)

### Lottes 2009/2011 — FXAA
- **Title**: "FXAA — Fast Approximate Anti-Aliasing"
- **Authors**: Timothy Lottes (NVIDIA)
- **Venue**: NVIDIA White Paper, 2009; FXAA 3.11, 2011
- **URL**: https://developer.download.nvidia.com/assets/gamedev/files/sdk/11/FXAA_WhitePaper.pdf
- **Used in**: `fxaa_ps.hlsl`
- **Key contribution**: Single-pass morphological edge AA using luminance gradients.

---

## 25. Volumetric Clouds (Advanced)

### Oz & Kulla 2017 — Multi-Scattering Octaves
- **Title**: "Production Volume Rendering"
- **Authors**: Frank Oz, Christopher Kulla (Pixar)
- **Venue**: SIGGRAPH 2017 Course: Production Volume Rendering
- **Used in**: `cloud_raymarch_cs.hlsl` (multi-scattering octave loop — planned Phase 2)
- **Key contribution**: Iterative multi-scattering approximation: each octave attenuates scattering (0.5x), extinction (0.5x), and phase eccentricity (0.5x). Produces bright silver linings and luminous cloud interiors.

### Bowles 2017 — Structured Volume Sampling
- **Title**: "Structured Volume Sampling" (volsample)
- **Authors**: Huw Bowles
- **Venue**: 2017
- **URL**: https://github.com/huwb/volsample
- **Used in**: `cloud_shadow_cs.hlsl` (planned Phase 4 — BSM shadow pass)
- **Key contribution**: Icosahedron-aligned plane intersections for temporally stable volume sampling at low resolution. Eliminates flickering in cloud shadow maps.

### Wrenninge et al. 2013 — Beer Shadow Maps
- **Title**: "Oz: The Great and Volumetric"
- **Authors**: Magnus Wrenninge, Nafees Bin Zafar, et al. (Sony Pictures Imageworks)
- **Venue**: SIGGRAPH 2013 Talk
- **Used in**: `cloud_shadow_cs.hlsl` (planned Phase 4 — BSM encoding)
- **Key contribution**: 4-channel shadow map encoding (frontDepth, meanExtinction, maxOpticalDepth, tailOpticalDepth) for efficient cloud shadow lookup.

### Peters 2019 — Spatio-Temporal Blue Noise
- **Title**: "Spatio-Temporal Blue Noise"
- **Authors**: Christoph Peters
- **Venue**: 2019
- **URL**: https://christophpeters.de/stbn
- **Used in**: `cloud_raymarch_cs.hlsl` (planned Phase 1 — jitter), STBN 3D texture (128x128x64 R8)
- **Key contribution**: Blue noise 3D textures providing better perceptual noise distribution than white noise for temporal accumulation. Indexed by `pixel.xy % 128, frame % 64`.

### NVIDIA 2018 — Draine Approximate Mie Phase Function
- **Title**: "Approximate Mie Scattering for Large Particles"
- **Authors**: NVIDIA (based on Draine 2003)
- **Used in**: `cloud_raymarch_cs.hlsl` (planned Phase 2 — optional Draine phase function)
- **Key contribution**: Numerically fitted Mie phase function for cloud droplets (d=10 large particles): gHG=0.988, gD=0.556, alpha=22.0, weight=0.482. More physically accurate than dual-lobe Henyey-Greenstein.

---

## 26. Open-Source Reference Implementations

### takram/three-geospatial — Atmosphere
- **Title**: "@takram/three-atmosphere"
- **Authors**: Shota Matsuda, Takram Design Engineering
- **License**: MIT (shaders: BSD-3-Clause from Bruneton reference)
- **URL**: https://github.com/takram-design-engineering/three-geospatial/tree/main/packages/atmosphere
- **Analysis**: `docs/analysis/rendering/three-geospatial-atmosphere-comparison.md`
- **Used as reference for**: Bruneton 2017 full 4D scattering LUT precomputation, irradiance LUT, runtime sky rendering via LUT lookup (no per-pixel march), CPU-side transmittance sampling for sun color.
- **Note**: MIT/BSD-3 licensed. Key insight: full 3D scattering LUT eliminates per-pixel sky march (256x128x32 RGBA16F, ~4MB VRAM, precomputed once). UzEngine reimplements techniques independently in DX12/HLSL.

### takram/three-geospatial — Volumetric Clouds
- **Title**: "@takram/three-clouds"
- **Authors**: Takram Design Engineering
- **License**: MIT
- **URL**: https://github.com/takram-design-engineering/three-geospatial/tree/main/packages/clouds
- **Analysis**: `docs/analysis/rendering/three-geospatial-clouds-adaptation.md`
- **Used as reference for**: Adaptive stepping, multi-scattering octaves, Beer Shadow Maps, Bayer temporal upscale, aerial perspective on clouds, multi-layer cloud system.
- **Note**: MIT-licensed. UzEngine reimplements techniques independently in DX12/HLSL compute shaders; no code is copied.
