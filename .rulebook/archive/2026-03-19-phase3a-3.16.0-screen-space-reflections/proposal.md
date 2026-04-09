# Screen-Space Reflections (SSR)

## Summary

Implement Hi-Z accelerated Screen-Space Reflections matching UE5's SSR pass. SSR traces reflection rays through the depth buffer to find on-screen reflected geometry, providing real-time glossy/mirror reflections without ray tracing hardware. Falls back to IBL cubemap probes for off-screen or rough surfaces.

## Motivation

IBL provides static environment reflections but cannot reflect dynamic objects or nearby geometry. SSR adds real-time reflections of on-screen content — essential for wet floors, metallic surfaces, water, and any reflective material. UE5 uses SSR as the default reflection method before Lumen RT.

## UE5 Reference Implementation

### Algorithm (UE5 ScreenSpaceReflections.usf)
- **Hi-Z ray march**: Build hierarchical Z-buffer (min-depth mip chain). Start at finest mip, step along reflection ray. When ray is behind depth → go finer mip. When ray is in front → go coarser mip (skip empty space). This is O(log N) vs O(N) linear march.
- **Binary refinement**: After Hi-Z finds approximate hit, do 4-8 binary search steps for precise intersection.
- **Roughness-based cone**: For rough surfaces, widen the search cone — sample multiple rays or use pre-filtered mip of scene color. UE5 uses roughness threshold to disable SSR on very rough surfaces (>0.6).
- **Fresnel weighting**: Reflections are stronger at grazing angles (Schlick Fresnel). SSR intensity = `F0 + (1-F0) * pow(1-NdotV, 5)`.
- **Screen-edge fade**: Fade reflections near screen edges where reflected content may not exist. UE5 uses smooth `1 - saturate(edgeDist / fadeWidth)`.
- **Temporal filtering**: SSR is noisy — use TAA history to accumulate and denoise reflections over multiple frames.

### UE5 Parameters (FScreenSpaceReflectionsParams)
| Parameter | UE5 Default | Description |
|-----------|-------------|-------------|
| `r.SSR.Quality` | 3 | 0=Off, 1=Low (linear), 2=Medium (Hi-Z), 3=High (Hi-Z+temporal), 4=Epic |
| `r.SSR.MaxRoughness` | 0.6 | Roughness above which SSR is disabled (too blurry to be useful) |
| `ScreenSpaceReflectionIntensity` | 100.0 | Reflection brightness (PostProcessVolume, 0-200) |
| `ScreenSpaceReflectionMaxRoughness` | 0.6 | Per-volume max roughness override |
| `ScreenSpaceReflectionQuality` | 50.0 | Quality 0-100, controls step count |
| Max steps | 12 (Low) / 32 (Med) / 64 (High) | Ray march iterations |
| Hi-Z mip levels | 6-8 | Hierarchical depth levels |
| Binary refinement steps | 8 | Post-hit precision steps |
| Temporal accumulation | Yes (High+) | Reproject + blend with TAA |

### UE5 Pipeline Position
After SSAO, before lighting composite. SSR output is blended into specular lighting term. Roughness > maxRoughness falls back to reflection probes / IBL.

## UzEngine Implementation Plan

### Shader Files
- `hiz_generate_cs.hlsl` — Compute: generate min-depth Hi-Z mip chain (6 levels)
- `ssr_trace_cs.hlsl` — Compute: Hi-Z ray march + binary refinement per pixel
- `ssr_resolve_ps.hlsl` — Pixel: sample scene color at hit UV, apply Fresnel, screen-edge fade, blend with IBL fallback
- `ssr_temporal_cs.hlsl` — Compute: temporal accumulation of SSR using velocity buffer

### Constant Buffer (register b2)
```hlsl
cbuffer SSRParams : register(b2) {
    float  maxRoughness;        // 0.6 UE5 default
    float  intensity;           // 1.0 (maps to UE5's 100.0 / 100)
    float  stepSize;            // Initial ray step size in view-space
    int    maxSteps;            // 12/32/64 based on quality
    int    binarySteps;         // 8 refinement steps
    float  fadeStart;           // Screen edge fade start (0.8 = 80% from center)
    float  fadeEnd;             // Screen edge fade end (1.0 = screen edge)
    float  thickness;           // Depth comparison thickness (prevents false hits)
    float2 texelSize;           // 1/resolution
    int    hizMipCount;         // Number of Hi-Z levels
    float  pad;
    float4x4 viewProj;         // For screen-space to world-space ray
    float4x4 invViewProj;      // For reconstruction
};
```

### Resources
- **Hi-Z buffer**: R32_FLOAT, mip chain (6-8 levels), min-depth reduction
- **SSR hit buffer**: RGBA16F (hitUV.xy + hitDepth + confidence)
- **SSR color**: RGBA16F (resolved reflection color)
- **SSR history**: RGBA16F for temporal accumulation

### Quality Levels (match UE5)
- **Low**: Linear ray march, 12 steps, no temporal, no Hi-Z → ~0.2ms
- **Medium**: Hi-Z ray march, 32 steps, 8 binary refinement, no temporal → ~0.5ms
- **High**: Hi-Z, 64 steps, 8 binary, temporal filtering → ~0.8ms
- **Epic**: High + roughness-based multi-sample (4 rays per pixel) → ~1.5ms

## Impact

- **Shaders**: 4 new HLSL files
- **Render graph**: Hi-Z generation pass + SSR trace pass + resolve + temporal
- **Textures**: Hi-Z chain + hit buffer + color + history
- **VRAM**: ~32MB at 1080p
- **Performance**: 0.2-1.5ms depending on quality
- **Dependencies**: Depth buffer, G-Buffer normals, roughness, scene color, velocity buffer (temporal)

## Breaking Changes

None — SSR is additive. Falls back to existing IBL when disabled or roughness too high.

## Acceptance Criteria

1. Reflective floor shows reflected geometry (cubes, spheres above it)
2. Reflections fade at screen edges (no hard cutoff)
3. Rough surfaces (>0.6) show no SSR (falls back to IBL)
4. Smooth surfaces show sharp reflections, mid-roughness shows blurred
5. Hi-Z ray march finds hits in <1ms at 1080p (no linear fallback needed)
6. Temporal filtering reduces SSR noise without ghosting
7. Demo screenshot: wet floor with reflected objects, side-by-side IBL-only vs SSR
