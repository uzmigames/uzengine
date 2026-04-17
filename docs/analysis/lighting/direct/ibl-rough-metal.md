<!-- doc-meta
title: IBL Rough Metal — Excessive Reflections Analysis
created: 2026-04-05
status: complete
engines-compared: Three.js, Filament, UzEngine
-->

# IBL Rough Metal — Excessive Reflections Analysis

## Problem

UzEngine's DarkMetal sphere (roughness=0.4, metallic=1.0, baseColor=0.4) shows
**excessive reflections** compared to Three.js reference. The sphere should appear
dark/opaque with subtle blurry environment reflections, but instead shows clearly
visible HDRI structure (bright spots, sky, room details).

## Root Cause: Prefiltered Cubemap Resolution Mismatch

**UzEngine bakes at 1024px base** vs Three.js PMREM at **256px base**. At roughness=0.4:

| Engine | Base Resolution | Mip at r=0.4 | Effective Face Size | Content |
|--------|----------------|--------------|---------------------|---------|
| UzEngine | 1024px | 2.8 | **147px** | Retains HDRI structure, bright spots visible |
| Three.js | 256px | 2.0 | **16px** | Fully averaged, near-constant color |
| Filament | 256px | 2.56 | ~24px | Mostly averaged |

**9.2x more detail** at the same roughness. Any bright sun/sky in the HDRI that
Three.js has averaged to a flat value is still a recognizable bright spot in UzEngine.

## DFG/Specular Math — Confirmed Correct

| Value | UzEngine | Three.js | Filament | Match? |
|-------|----------|----------|----------|--------|
| envBRDF.x (r=0.4, NoV=0.5) | 0.835 | 0.894 (LUT) | 0.927 (LUT) | Close |
| envBRDF.y | 0.0 (clamped) | 0.029 | 0.029 | Minor diff |
| singleScatter | 0.334 | 0.387 | 0.388 | UzEngine LOWER |
| multiScatter (metals) | **0.0 (suppressed)** | 0.025 | included | UzEngine wrong |
| Net radiance multiplier | 0.334 | 0.387 | 0.400 | UzEngine LOWEST |

**The specular math is NOT the cause of excessive brightness.** UzEngine's
multiplier (0.334) is actually lower than Three.js (0.387). The excess comes
entirely from the prefiltered cubemap content.

## Secondary Issue: Multi-Scatter Suppression for Metals

```hlsl
// frame_gi_passes.cpp line 409
float msWeight = saturate((1.0 - metallic) * 10.0); // = 0 for metallic=1
```

Three.js and Filament both include multi-scatter for metals. UzEngine suppresses
it entirely. This is physically wrong (metals DO have multi-scatter bounce) but
makes the result slightly LESS bright, partially masking the prefilter issue.

## Tertiary Issue: Stale Default Constants

Three files have `prefilteredMipLevels = 5` defaults that don't match the
baker's `kPrefilteredMipLevels = 8`:
- `per_frame_params.h`
- `constant_buffers.h`
- `multi_light.h`

In normal operation `frame_constants.cpp` overrides with the correct value 8,
but these stale defaults are a reliability risk.

## Recommended Fixes (Priority Order)

### Fix 1: Reduce prefiltered cubemap base resolution (PRIMARY)

Change `kPrefilteredSize` from 1024 to 256 in `ibl_baker.h`, or make the baker
respect the CVar `r.IBL.PrefilteredSize` (which already defaults to 256).

With 256px base and 8 mips: roughness=0.4 → mip 2.8 → effective face ~36px.
Much closer to Three.js's 16px, HDRI hotspots mostly averaged out.

### Fix 2: Remove multi-scatter suppression for metals

Replace:
```hlsl
float msWeight = saturate((1.0 - metallic) * 10.0);
float3 specularAmbient = radiance * singleScatter
                       + cosineIrradiance * multiScatter * msWeight;
```
With:
```hlsl
float3 specularAmbient = radiance * singleScatter
                       + cosineIrradiance * multiScatter;
```

Per Three.js and Filament: multi-scatter applies to all materials unconditionally.

### Fix 3: Update stale prefilteredMipLevels defaults

Change `5` → `8` in per_frame_params.h, constant_buffers.h, multi_light.h to
match `IBLBaker::kPrefilteredMipLevels`.

## LOD Mapping Comparison

| Engine | Formula | r=0.4 → mip | Type |
|--------|---------|-------------|------|
| Three.js | Piecewise breakpoints | 2.0 | Non-linear |
| Filament | `maxMip * r * (2-r)` | 2.56 | Quadratic |
| UzEngine | `r * (maxMip - 1)` | 2.8 | Linear |

UzEngine's linear mapping is simpler but samples slightly blurrier mips at
mid-roughness compared to Three.js piecewise. This is a minor visual difference.
