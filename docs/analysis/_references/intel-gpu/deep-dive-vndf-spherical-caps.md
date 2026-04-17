<!-- doc-meta
date: 2026-04-08
paper: "Sampling Visible GGX Normals with Spherical Caps"
authors: Jonathan Dupuy, Anis Benyoub (Intel Corporation)
venue: High-Performance Graphics (HPG) 2023
arxiv: 2306.05044
pdf: https://cdrdv2-public.intel.com/782052/sampling-visible-ggx-normals.pdf
code: https://gist.github.com/jdupuy/4c6e782b62c92b9cb3d13fbb0a5bd7a0
license: CC BY 4.0
-->

# Deep Dive: Sampling Visible GGX Normals with Spherical Caps

## TL;DR for UzEngine

**What**: Drop-in replacement for our GGX VNDF sampler in `pbr_common.hlsli`.
**Why**: Simpler code, 37-53% faster in isolation, 2.4-2.55x faster BSDF sampling relative time.
**How**: Replace hemisphere cross-section sampling (Heitz 2018) with spherical cap sampling.
**Risk**: Zero — mathematically proven to produce identical distributions.

---

## 1. The Problem

GGX microfacet BRDFs (Trowbridge-Reitz) are the standard in every modern renderer
(Frostbite, Unreal, Unity, PBRT, Disney, Pixar, DreamWorks, Sony Imageworks).
Importance sampling the GGX VNDF (Visible Normal Distribution Function) is critical
for low-variance Monte Carlo estimation of surface reflectance.

The state-of-the-art method (Heitz 2018, JCGT) works in 3 steps:
1. Warp space with inverse matrix M^-1 to map GGX ellipsoid → unit hemisphere
2. Sample visible normals of the hemisphere via cross-section parameterization
3. Warp back to ellipsoid configuration

Step 2 is the complex part — Heitz uses the cross section of the hemisphere, which
requires building an orthonormal basis, parameterizing the cross section, and
reprojecting onto the hemisphere. This paper replaces step 2 entirely.

## 2. The Key Insight

> "A hemispherical mirror reflects parallel light rays uniformly within a solid angle
> shaped as a spherical cap."

If the hemisphere used for VNDF sampling acted as a perfect mirror, incident parallel
rays from direction **i** would be reflected uniformly into a spherical cap. The cap:
- Has the same apex as the hemisphere (pointing up, z-axis)
- Is bounded at elevation z = -z_i (the negated z-component of the incident direction)
- Has solid angle = 2π(1 + z_i)

This means: **sampling the visible hemisphere is equivalent to sampling a spherical cap
and computing the half-vector.** The half-vector between the incident direction and
a uniformly sampled cap direction IS a visible normal.

### Mathematical proof (from paper)

The density of outgoing directions reflected by the hemisphere mirror is:

```
f_p(o, i) = D_vis,std(h, i) * |dh/do|
```

Using the reflection Jacobian `|dh/do| = 1 / (4|o·h|)` and the hemisphere NDF
`D_std(m) = 1/π for z_m > 0`, the density becomes:

```
f_p(o, i) = D_std(h) / (4 * σ_std(i))
           = (1/π) / (4 * (1+z_i)/2)
           = 1 / (2π(1+z_i))    if z_h > 0 (equivalently z_o > -z_i)
           = 0                   otherwise
```

This is a **uniform distribution** over a spherical cap bounded at z = -z_i. QED.

## 3. The Algorithm

### Step-by-step:

1. **Sample spherical cap**: Draw direction **o** uniformly from cap with elevation -z_i
2. **Compute half-vector**: h = normalize(o + i)
3. **Return h** as the visible normal

### Full GGX VNDF sampling (3 stages, same as Heitz):

1. **Warp to hemisphere**: `wiStd = normalize(wi.xy * alpha, wi.z)`
2. **Sample hemisphere via spherical cap** (our new step 2)
3. **Warp back to ellipsoid**: `wm = normalize(wmStd.xy * alpha, wmStd.z)`

## 4. HLSL Implementation for UzEngine

### Reference GLSL from paper (Listing 3 + Listing 1):

```hlsl
/// @ref Dupuy & Benyoub 2023 "Sampling Visible GGX Normals with Spherical Caps"
/// @ref HPG 2023, arXiv:2306.05044
/// @constants PI = 3.14159265358979323846

// Sample visible hemisphere normals via spherical cap (replaces Heitz 2018 cross-section)
float3 SampleVndf_Hemisphere(float2 u, float3 wi)
{
    // Sample a spherical cap in (-wi.z, 1]
    float phi = 2.0f * PI * u.x;
    float z = mad((1.0f - u.y), (1.0f + wi.z), -wi.z);  // = (1-u.y)*(1+wi.z) - wi.z
    float sinTheta = sqrt(saturate(1.0f - z * z));
    float x = sinTheta * cos(phi);
    float y = sinTheta * sin(phi);
    float3 c = float3(x, y, z);

    // Compute halfway direction (skip normalize — done in caller)
    float3 h = c + wi;
    return h;
}

// Full GGX VNDF sampler — drop-in replacement
float3 SampleVndf_GGX(float2 u, float3 wi, float2 alpha)
{
    // (1) Warp to hemisphere configuration
    float3 wiStd = normalize(float3(wi.xy * alpha, wi.z));

    // (2) Sample the hemisphere via spherical cap
    float3 wmStd = SampleVndf_Hemisphere(u, wiStd);

    // (3) Warp back to ellipsoid configuration
    float3 wm = normalize(float3(wmStd.xy * alpha, wmStd.z));

    return wm;
}
```

### World-space isotropic-only version (from Dupuy's gist):

```hlsl
/// @ref Dupuy & Benyoub 2023 (isotropic world-space variant)
float3 SampleVndf_GGX_WorldSpace(float2 u, float3 wi, float alpha, float3 n)
{
    // Decompose into parallel and perpendicular components
    float3 wi_z = n * dot(wi, n);
    float3 wi_xy = wi - wi_z;

    // Warp to hemisphere configuration
    float3 wiStd = normalize(wi_z - alpha * wi_xy);

    // Sample spherical cap in (-wiStd·n, 1]
    float wiStd_z = dot(wiStd, n);
    float phi = (2.0f * u.x - 1.0f) * PI;
    float z = (1.0f - u.y) * (1.0f + wiStd_z) - wiStd_z;
    float sinTheta = sqrt(saturate(1.0f - z * z));
    float x = sinTheta * cos(phi);
    float y = sinTheta * sin(phi);
    float3 cStd = float3(x, y, z);

    // Reflect sample to align with normal
    float3 up = float3(0, 0, 1);
    float3 wr = n + up;
    float3 c = dot(wr, cStd) * wr / wr.z - cStd;

    // Compute halfway direction
    float3 wmStd = c + wiStd;
    float3 wmStd_z = n * dot(n, wmStd);
    float3 wmStd_xy = wmStd_z - wmStd;

    // Warp back to ellipsoid
    float3 wm = normalize(wmStd_z + alpha * wmStd_xy);
    return wm;
}
```

## 5. PDF Evaluation

The VNDF PDF for importance sampling (needed for Monte Carlo weight calculation):

```
PDF(sample_dir, view_dir) = G1(m, i) * D(m) / (4 * cos(theta_i))
```

Where:
- `D(m)` = GGX NDF (Trowbridge-Reitz)
- `G1(m, i)` = Smith monostatic shadowing term
- `m` = half-vector between sample and view directions

**This PDF is UNCHANGED from Heitz 2018.** Only the sampling method changes — the
distribution it samples is mathematically identical. Existing PDF evaluation code
does not need modification.

## 6. Performance Benchmarks (from paper)

### Synthetic benchmarks (VNDF sampling in isolation):

| Platform | Speedup vs Heitz 2018 |
|----------|----------------------|
| Intel i7-13700K (CPU) | **37.67%** |
| Intel Arc A770 (GPU) | **52.96%** |
| NVIDIA RTX 2080 (GPU) | **39.25%** |

### Relative BSDF sampling time in PBRT-v4 renders:

| Scene | Heitz 2018 | Ours | Speedup |
|-------|-----------|------|---------|
| pbrt-book | 1.34% | 0.54% | **2.48x** |
| lte-orb (conductor) | 3.71% | 1.49% | **2.49x** |
| lte-orb (dielectric) | 1.89% | 0.74% | **2.55x** |
| sportscar | 0.48% | 0.20% | **2.40x** |

### Full render time (PBRT-v4, 1024 spp):

| Scene | Heitz 2018 | Ours | Speedup |
|-------|-----------|------|---------|
| Rough conductor | 149.0s | 138.5s | **+7.58%** |
| Rough dielectric | 1039.1s | 1006.1s | **+3.28%** |

Note: Full render speedup is modest because BSDF sampling is <4% of total render
time — the rest is ray-BVH intersection and memory access. But for UzEngine's
real-time renderer where GGX evaluation is called per-pixel per-frame, the GPU
speedup (39-53%) translates to meaningful frame time savings.

## 7. Why It's Faster

Heitz 2018 (Listing 2 in paper) requires:
- Build orthonormal basis from `wi` (branch for degenerate case)
- `sqrt`, `cos`, `sin` for cross-section parameterization
- Cross-section reprojection with `s = (1+wi.z)/2` blending
- `sqrt` for reprojection onto hemisphere
- Matrix multiply (3 dot products) for basis transformation

**Total: 2 sqrt, 1 cos, 1 sin, 1 inversesqrt, 3 muls, branch**

Spherical caps (Listing 3) requires:
- `cos`, `sin` for phi
- `mad` for z (fused multiply-add)
- `sqrt` for sinTheta
- Vector add (c + wi)

**Total: 1 sqrt, 1 cos, 1 sin, 1 mad, 1 add — no branch, no basis construction**

The elimination of the orthonormal basis construction and reprojection step is the
key savings.

## 8. Code Comparison (Heitz vs Ours)

### Heitz 2018 — hemisphere sampling via cross-section (20 lines):
```hlsl
float3 SampleVndf_Hemisphere_Heitz(float2 u, float3 wi)
{
    // Build orthonormal basis (with branch for degenerate case)
    float tmp = wi.x * wi.x + wi.y * wi.y;
    float3 w1 = tmp > 0.0f ? float3(-wi.y, wi.x, 0) * rsqrt(tmp)
                            : float3(1, 0, 0);
    float3 w2 = cross(wi, w1);
    // Parameterize cross section
    float phi = 2.0f * PI * u.x;
    float r = sqrt(u.y);
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = (1.0f + wi.z) / 2.0f;
    t2 = (1.0f - s) * sqrt(1.0f - t1 * t1) + s * t2;
    float ti = sqrt(max(1.0f - t1 * t1 - t2 * t2, 0.0f));
    // Reproject onto hemisphere
    float3 wm = t1 * w1 + t2 * w2 + ti * wi;
    return wm;
}
```

### Ours — hemisphere sampling via spherical cap (14 lines):
```hlsl
float3 SampleVndf_Hemisphere_SphericalCap(float2 u, float3 wi)
{
    // Sample spherical cap in (-wi.z, 1]
    float phi = 2.0f * PI * u.x;
    float z = mad((1.0f - u.y), (1.0f + wi.z), -wi.z);
    float sinTheta = sqrt(saturate(1.0f - z * z));
    float x = sinTheta * cos(phi);
    float y = sinTheta * sin(phi);
    float3 c = float3(x, y, z);
    // Half-vector (normalize deferred to caller)
    float3 h = c + wi;
    return h;
}
```

## 9. Integration Plan for UzEngine

### What to change:
1. Replace `SampleVndf_Hemisphere` in `pbr_common.hlsli` with spherical cap version
2. No change to PDF evaluation (identical distribution)
3. No change to reflection/refraction code downstream
4. No change to constant buffers or material parameters

### What to verify:
1. `--pixel-debug` at rough metallic surfaces: VNDF samples should produce identical
   distribution statistics (mean, variance) as before
2. `--screenshot-stdout` comparison: renders should be pixel-identical at high spp
3. Path tracer mode (if active): convergence rate should be identical

### Affected shaders:
- `pbr_common.hlsli` — VNDF sampler function
- `mesh_ps.hlsl` — indirectly via includes
- Any future SSR/path-tracing shader that importance-samples GGX
- IBL prefilter convolution (if using VNDF importance sampling)

### Estimated effort: 2-3 days
- Day 1: Implement + verify pixel-debug parity
- Day 2: Screenshot comparison at multiple roughness values
- Day 3: Performance profiling (GPU timestamp queries)

## 10. References (from paper)

| Ref | Citation |
|-----|----------|
| [Hei18] | Heitz E., "Sampling the GGX Distribution of Visible Normals", JCGT 7(4), 2018 |
| [Hd14] | Heitz E., d'Eon E., "Importance Sampling Microfacet-Based BSDFs Using the Distribution of Visible Normals", CGF 33(4), 2014 |
| [TR75] | Trowbridge T.S., Reitz K.P., "Average Irregularity Representation of a Rough Surface for Ray Reflection", JOSA 65(5), 1975 |
| [WMLT07] | Walter B., Marschner S.R., Li H., Torrance K.E., "Microfacet Models for Refraction Through Rough Surfaces", EGSR 2007 |
| [CT82] | Cook R.L., Torrance K.E., "A Reflectance Model for Computer Graphics", ACM TOG 1(1), 1982 |
| [Smi67] | Smith B., "Geometrical Shadowing of a Random Rough Surface", IEEE Trans. AP 15(5), 1967 |
| [AKDW22] | Atanasov A., Koylazov V., Dimov R., Wilkie A., "Microsurface Transformations", CGF 41(4), 2022 |
