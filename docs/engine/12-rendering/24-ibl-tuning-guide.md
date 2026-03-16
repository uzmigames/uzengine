# IBL Tuning Guide

<!-- doc-meta
title: IBL Tuning Guide
category: guide
engine: uzengine
theme: rendering
created: 2026-03-16
updated: 2026-03-16
status: active
author: docs-operator
-->

## Overview

This guide provides practical strategies for tuning IBL (Image-Based Lighting) quality, performance, and visual appearance. It covers resolution tradeoffs, environment map sourcing, artist-facing parameters, and troubleshooting common issues.

## Quality vs. Performance Tradeoffs

### BRDF LUT Resolution

| Resolution | Memory | Quality | Recommended For |
|-----------|--------|---------|-----------------|
| 256×256 | 256 KB | Low (banding at high roughness) | Mobile/low-end |
| **512×512** | **1 MB** | **Medium (default)** | **Recommended** |
| 1024×1024 | 4 MB | High (imperceptible improvement) | High-end/screenshots |

**Current implementation**: 512×512 (GPU bake time: 5–10ms)

**Decision rule**: For most projects, 512×512 is sufficient. Increase to 1024×1024 only if visible banding occurs in grazing-angle specular highlights on polished surfaces.

### Irradiance Cubemap Resolution

| Resolution | Memory (6 faces) | Quality | Bake Time |
|-----------|-----------------|---------|-----------|
| 16×16 | 24 KB | Very low (visible blocky artifacts) | <1ms |
| **32×32** | **96 KB** | **Medium (default)** | **1–2ms** |
| 64×64 | 384 KB | High (subtle color banding) | 3–5ms |
| 128×128 | 1.5 MB | Very high (diminishing returns) | 10–20ms |

**Current implementation**: 32×32 (sufficient for diffuse ambient, which is naturally blurred).

**Decision rule**: Keep at 32×32 unless you see posterization in shadowed areas or diffuse specular transitions. 64×64 is overkill for diffuse-only application.

### Prefiltered Specular Cubemap

| Resolution | Mips | Memory | Bake Time | Best For |
|-----------|------|--------|-----------|----------|
| 64×64 | 4 | 384 KB | 5ms | Budget projects |
| **128×128** | **5** | **1.1 MB** | **15–20ms** | **Default (recommended)** |
| 256×256 | 6 | 4.2 MB | 50–70ms | High-detail environments |

**Current implementation**: 128×128 with 5 mips (LOD 0–4 covering roughness 0.0–1.0).

**Decision rule**:
- **128×128** for typical outdoor/indoor scenes (smooth transitions at all roughness values)
- **64×64** if you need < 1ms per-frame IBL bake and accept lower specular detail
- **256×256** only for close-up shots of reflective surfaces where mirror-like reflections matter

### Sample Count per Texel

| Samples | Quality | Bake Time (512×512) | Notes |
|---------|---------|-------------------|-------|
| 64 | Very low (noise) | 2–3ms | Not recommended |
| 256 | Low (some noise) | 5–8ms | Mobile acceptable |
| **1024** | **Medium (default)** | **15–20ms** | **Good balance** |
| 4096 | High (very clean) | 50–80ms | Offline/screenshot |

**Current implementation**: 1024 samples (good noise-to-time tradeoff).

**Decision rule**: Stay with 1024. Increase to 4096 only for final production screenshots. Noise converges logarithmically, so doubling samples yields only ~3% quality gain.

## Environment Map Sources

### Procedural Sky Cubemap (Current)

UzEngine generates environment maps procedurally from `SkyAtmosphereComponent`:

```cpp
struct SkyAtmosphereComponent {
    // Atmospheric scattering parameters
    float  rayleighScatteringScale;  // 1.0 = realistic Rayleigh
    float  mieScatteringScale;        // 0.0–1.0 (adds haze)
    float  skyLuminanceFactor;        // Overall brightness multiplier

    // Technical parameters
    float  artisticColorSaturation;   // >1.0 for more vibrant colors
};
```

**Advantages**:
- No external HDR capture needed
- Physically correct daylight/atmosphere
- Smooth transitions across time of day
- Seamless sky-to-mesh color matching

**Disadvantages**:
- Limited realism (no clouds, complex weather)
- Requires artistic tuning of scattering parameters
- Sky-to-ground color mismatch if artist parameters are wrong

**Tuning strategy**:
1. Set `rayleighScatteringScale = 1.0` for physical accuracy
2. Adjust `mieScatteringScale` (0.0–0.5) to add atmospheric haze
3. Use `skyLuminanceFactor` to brighten/darken entire sky
4. Tweak `artisticColorSaturation` (1.0–1.5) if colors look washed out

### HDR Equirectangular Maps (Planned Phase 3)

Future support for importing `.hdr` files (e.g., from Poly Haven, Ambient CG):

```
my_environment.hdr → AssetPipeline → environment_cubemap.uzpak
```

**When to use**:
- Photorealistic indoor scenes
- Architecture/product visualization
- Ported content from other engines

**Integration path** (placeholder):
```cpp
// Phase 3: planned API
auto env_cubemap = assets::LoadTexture("environments/my_env.hdr");
skylighting.cubemap = env_cubemap;
ibl_baker.BakeEnvironment(env_cubemap);
```

### Cubemap Capture from Scene

Advanced technique: capture real-time scene reflection into cubemap (mirrors, chrome).

**Not yet implemented.** Would require:
1. Render scene from 6 directions (cube faces)
2. Save as cubemap texture
3. Feed to IBL baker
4. Composite reflections into final render

**When useful**: Dynamic environments, water reflections, real-time relighting.

## SH vs. Cubemap Sampling

### Diffuse: Use Spherical Harmonics (SH)

**Why SH?**
- Diffuse lighting is naturally smooth (low-frequency)
- L2 SH (9 coefficients) captures 90%+ of diffuse information
- O(1) GPU evaluation: just 9 dot products per pixel
- Negligible bandwidth cost (112 bytes in PerFrameCB)

**SH quality**:
- **L0 only**: Constant color (very poor, never use)
- **L1 (4 coeffs)**: 90° directional transitions (acceptable for simple scenes)
- **L2 (9 coeffs)**: Full detail up to ~60° feature scale (recommended)
- **L3 (16 coeffs)**: Overkill for diffuse (not implemented)

**Rule**: Always use L2 SH for diffuse ambient.

### Specular: Use Cubemap Sampling

**Why cubemaps?**
- Specular highlights are high-frequency (depend on view angle)
- Need full directional detail to show mirror-like reflections
- Prefiltered LOD selection by roughness is natural and efficient

**Cubemap quality**:
- **64×64, 4 mips**: Budget (occasional aliasing on rough surfaces)
- **128×128, 5 mips**: Standard (smooth across all roughness)
- **256×256, 6 mips**: High (for close-up shots, not needed for gameplay)

**Rule**: Use prefiltered cubemap for specular reflection evaluation in pixel shader.

## SkyLight Component Parameters

All parameters exposed on `SkyLight` component are artist-controlled (no shader recompile):

### `intensity` (Default: 1.0)

Multiplier on captured irradiance. Controls overall ambient brightness.

```cpp
ambient = evaluate_sh(...) * skylight.intensity;
```

| Value | Effect | Use Case |
|-------|--------|----------|
| 0.5 | Half brightness | Dimly lit indoor scenes |
| **1.0** | **Full brightness** | **Normal (default)** |
| 2.0 | Double brightness | Bright outdoor scenes, overexposed feel |
| 4.0 | Very bright | Extremely bright outdoor (desert, snow) |

**Tuning tip**: Adjust intensity first if scene is too bright or dark. This affects all ambient lighting uniformly.

### `color` (Default: white)

RGB tint applied to irradiance before evaluation.

```cpp
ambient = evaluate_sh(...) * skylight.intensity * skylight.color;
```

| Color | Effect |
|-------|--------|
| White (1,1,1) | No change |
| Blue-tinted | Cold, icy feel |
| Orange-tinted | Warm, sunset feel |
| Saturated values | Unrealistic but stylized |

**Tuning tip**: Use color to match scene mood (warm interiors, cool exteriors).

### `min_ambient` (Default: 0.04)

Floor value preventing fully black shadowed areas. Prevents loss of shape definition in deeply shadowed corners.

```cpp
ambient = max(evaluate_sh(...) * intensity * color, min_ambient);
```

| Value | Effect |
|-------|--------|
| 0.0 | Pure black shadows (high contrast, risky) |
| **0.04** | **Subtle fill (default)** |
| 0.1 | Noticeable fill (bright, washes out) |
| 0.2 | Strong fill (cartoon-like) |

**Tuning tip**: Keep in range 0.02–0.08. Values below 0.02 lose shape; above 0.1 lose contrast.

### `dfao_strength` (Default: 0.5)

Multiplier on Distance Field Ambient Occlusion (DFAO) affecting ambient contribution.

```cpp
ao_factor = 1.0 - (ao_texture * dfao_strength);
ambient *= ao_factor;
```

| Value | Effect |
|-------|--------|
| 0.0 | Ignore AO (ambient everywhere) |
| 0.5 | 50% AO influence (balanced) |
| 1.0 | Full AO darkening (cave-like) |

**Tuning tip**: Set to 0.5 unless scene explicitly needs darker shadows. Higher values (0.7–1.0) work better in outdoor environments with occluding geometry.

### `enabled` (Default: true)

Master switch for ambient lighting. If false, ambient = (0, 0, 0).

```cpp
if (!skylight.enabled) {
    ambient = 0;  // Only direct lighting remains
}
```

**Use case**: Entirely artificially-lit scenes (stage, studio) where natural lighting is unwanted.

## Artistic Scattering Controls

Parameters on `SkyAtmosphereComponent` affect captured sky cubemap **only** (not extinction/transmittance):

### `rayleighScatteringScale` (Default: 1.0)

Multiplier on Rayleigh scattering coefficient (affects blue sky color).

| Value | Effect |
|-------|--------|
| 0.5 | Pale, washed-out sky |
| **1.0** | **Physically realistic** |
| 2.0 | Deep blue sky (stylized) |

**Physically realistic**: 1.0 (represents actual atmospheric Rayleigh scattering)

**Tuning tip**: Increase for more vibrant blue. Decrease for hazy, polluted atmospheres.

### `mieScatteringScale` (Default: 0.0)

Multiplier on Mie scattering (affects haze color, often orangish).

| Value | Effect |
|-------|--------|
| 0.0 | Clear, no haze |
| 0.1 | Subtle haze (realistic) |
| 0.5 | Fog-like haze (artistic) |
| 1.0+ | Very opaque (dust storm, sandstorm) |

**Tuning tip**: Keep < 0.3 for realism. Increase for dusty/smoky atmospheres.

### `skyLuminanceFactor` (Default: 1.0)

Overall brightness multiplier on sky cubemap.

| Value | Effect |
|-------|--------|
| 0.5 | Dim, twilight-like |
| **1.0** | **Normal brightness** |
| 2.0 | Very bright (bleached-out, overexposed) |

**Rule**: Use `skyLuminanceFactor` to adjust overall sky brightness. Use `skylight.intensity` to adjust how much the sky affects ambient lighting.

## Common Issues & Solutions

### Black/Dark Reflections on Metallic Surfaces

**Symptom**: Metallic objects (chrome, mirrors) appear black instead of reflecting sky.

**Root cause**: BRDF LUT or prefiltered specular cubemap not baked.

**Solution**:
1. Ensure `IBLBaker::Initialize()` called in startup
2. Confirm `SkyLight` component exists in scene
3. Check that `ibl_baker.BakeEnvironment()` called each frame (or when sky dirty)
4. Verify descriptor heap has space for IBL SRVs (check root signature layout in [14-register-binding-convention.md](14-register-binding-convention.md))

### Blue Tint / Color Cast

**Symptom**: SH9-evaluated diffuse has visible blue bias (zenith color too dominant).

**Root cause**: GPU readback of cubemap pixels failed; fallback to analytic Rayleigh SH.

**Solution**:
1. Check if device supports GPU readback (D3D12 GPU visible state)
2. If on low-end device, increase sample count (1024 → 4096) to reduce noise
3. Or disable DFAO (`dfao_strength = 0`) to reduce influence of per-pixel AO
4. Fallback is correct but less accurate; higher-end platforms should work

### Over-Bright Ambient (Washed Out)

**Symptom**: Scene is too bright; shadows don't read as dark.

**Root cause**: `skylight.intensity` too high or `min_ambient` too aggressive.

**Solution**:
```cpp
// Reduce intensity
skylight.intensity = 0.5f;

// Reduce min_ambient to allow darker shadows
skylight.min_ambient = 0.02f;

// Or reduce both
skylight.intensity = 0.8f;
skylight.min_ambient = 0.03f;
```

### Flat Lighting / Loss of Dimension

**Symptom**: Scene looks flat even with directional sun light.

**Root cause**: SkyLight overshadowing direct lighting; or direct light too weak.

**Solution**:
1. Reduce `skylight.intensity` (ambient shouldn't dominate)
2. Increase sun light `intensity` (make direct light stronger)
3. Increase `sun.min_roughness` (sharpen sun specular highlights)

## Debugging & Validation

### Visual Validation

1. **Capture screenshots** with `-screenshot` flag
2. **Compare against reference** (UE5, Unity, real photograph)
3. **Check for artifacts**:
   - Banding in dark areas (increase BRDF LUT resolution)
   - Noise in sky reflections (increase sample count)
   - Blocky irradiance transitions (increase irradiance resolution)
   - Aliasing on rough reflections (increase prefiltered resolution)

### CPU Readback Diagnostic

Enable debug readback to inspect actual SH9 values:

```cpp
// In IBLBaker::BakeEnvironment()
if (cvar_ibl_debug_readback) {
    auto sh = ReadbackCubemapToSH(irradiance_cubemap);
    UZ_LOG(Rendering, Info, "SH[0] = {}, {}, {}", sh.coeff[0].x, sh.coeff[0].y, sh.coeff[0].z);
    // Expected: bright color (sky color)
}
```

**Expected SH values**:
- **SH[0]** (L0): Bright (sky blue/orange), ~(0.3–0.5, 0.3–0.5, 0.4–0.6)
- **SH[1-8]** (L1-L2): Directional modulation, typically ±(0.1–0.2, 0.1–0.2, 0.1–0.2)

### RenderDoc Inspection

Use RenderDoc to inspect live textures:

1. **BRDF LUT**: Should show smooth gradient from dark (rough) to bright (shiny)
2. **Irradiance cubemap**: Should show blurred sky color (blue zenith, orange horizon)
3. **Prefiltered specular**:
   - LOD 0: Should match sky cubemap (sharply defined)
   - LOD 4: Should be heavily blurred (dominated by sky color average)

## Phase Roadmap

| Phase | Feature | Status |
|-------|---------|--------|
| 1.3 | IBL with procedural sky, SH9 diffuse, prefiltered specular | ✅ Done |
| 2.0 | HDR equirectangular import (`.hdr` files) | Planned |
| 2.5 | Per-pixel DFAO occlusion maps | Planned |
| 3.0 | Real-time cubemap capture for dynamic reflections | Planned |
| 3.5 | Spherical GaussianOptimization (SG) alternative to SH | Planned |
| 4.0 | Radiance caching for global illumination | Planned |

## Key Takeaways

- **Resolution**: 512×512 BRDF LUT, 32×32 irradiance, 128×128 prefiltered cubemap
- **SH for diffuse**: L2 SH9 (9 coeffs) encodes 90%+ of diffuse detail
- **Cubemaps for specular**: Prefiltered with roughness LOD selection
- **Tuning order**: Start with `skylight.intensity`, then `min_ambient`, then `color`
- **DFAO**: Typically 0.5 (50% occlusion strength); increase to 1.0 for darker shadows
- **Artistic parameters**: Adjust `rayleighScatteringScale` + `mieScatteringScale` for sky mood
- **Common fix**: Black reflections = check IBL bake; washed out = reduce intensity

## References

- [23-ibl-lighting.md](23-ibl-lighting.md) — Technical details on BRDF LUT, irradiance, prefiltered maps
- [21-renderer-passes.md](21-renderer-passes.md) — Sky Atmosphere and IBL baking passes
- Poly Haven — Free HDR maps: https://polyhaven.com/hdris

## Changelog

| Date | Change |
|------|--------|
| 2026-03-16 | Initial creation — practical IBL tuning guide for artists and programmers |
