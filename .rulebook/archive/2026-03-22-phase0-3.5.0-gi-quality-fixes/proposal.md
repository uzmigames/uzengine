# Proposal: GI Quality Fixes — Shadow Bleed, Temporal Pulsing, Specular Instability, SSGI Noise

## Priority: P0 — Visible rendering defects in every frame

## Summary

Visual inspection of the PBR Test demo reveals 4 interconnected GI quality issues that degrade perceived rendering quality:

1. **Washed-out shadows** — Shadow areas under spheres are near-white, lacking contrast. Radiance Cache blending uses `max(diffuseIBL, rcDiffuse)` which injects light into shadowed regions. SSGI at quality=1 (4 rays) also bleeds sky luminance onto the ground plane.
2. **Sphere pulsing** — Brightness oscillation during camera movement. TAA Halton 8-sample jitter + RadianceCache EMA alpha=0.05 (20-frame convergence) + SSGI temporal — 3 temporal systems fighting each other.
3. **Gold sphere color shift** — Metallic gold sphere changes hue with view angle. Prefiltered specular cubemap is 128x128 (too low for metals). Metals use albedo as F0 — low-res envmap causes color shift as mip selection changes.
4. **Poor overall definition** — SSGI quality=1 (only 4 rays/pixel) is noisy, prefiltered 128x128 is low-res, SSGI sky bleeding creates bright halos.

## Why

These are the first things a user notices when looking at the renderer. Washed shadows and pulsing artifacts instantly signal "prototype quality". The gold color shift is a classic IBL resolution bug. The SSGI sky bleeding was previously identified (phase0-3.3.0) but never fixed. This task consolidates all GI quality issues into one focused effort.

## What Changes

### Fix 1: Radiance Cache shadow-aware blending (deferred_lighting_ps.hlsl:1145)
- Replace `max(diffuseIBL, rcDiffuse)` with `diffuseIBL + rcDiffuse * rcWeight * ao` — additive blend modulated by AO to prevent light injection in occluded areas
- Multiply radiance cache contribution by direct shadow factor

### Fix 2: SSGI sky rejection (ssgi_trace_cs.hlsl)
- Reject ray hits where depth >= farPlane * 0.99 (sky pixels)
- Clamp SSGI contribution from sky-hitting rays to ambient level
- Prevents bright white halos under objects on the ground plane

### Fix 3: SSGI quality & denoise (ssgi_renderer.h, ssgi_spatial_denoise_cs.hlsl)
- Increase default quality from 1 (4 rays) to 2 (8 rays)
- Review spatial denoise kernel and edge-stop functions
- Review temporal accumulation blend factor

### Fix 4: Temporal stability (radiance_cache.h, ssgi_temporal_accum_cs.hlsl)
- Increase RadianceCache EMA alpha from 0.05 to 0.12 (faster convergence, less pulsing)
- Add TAA jitter compensation to radiance cache probe ray origins
- Ensure SSGI temporal reprojection handles jitter correctly

### Fix 5: Prefiltered specular resolution (ibl_baker.h/cpp)
- Increase kPrefilteredSize from 128 to 256
- Increase kPrefilteredMipLevels from 5 to 6 (256→128→64→32→16→8)
- Update bake dispatch and PerFrameCB default

## Impact
- Affected specs: `docs/specs/rendering/ibl-pipeline.md`, `docs/specs/screen-space-effects.md`
- Affected code: `deferred_lighting_ps.hlsl`, `ssgi_trace_cs.hlsl`, `ssgi_spatial_denoise_cs.hlsl`, `ssgi_temporal_accum_cs.hlsl`, `radiance_cache.h`, `radiance_cache_trace_cs.hlsl`, `ssgi_renderer.h`, `ibl_baker.h`, `ibl_baker.cpp`
- Breaking change: NO
- User benefit: Dramatically improved visual quality — darker contact shadows, stable indirect lighting, correct metallic reflections, sharper overall rendering

## Acceptance Criteria

1. Shadows under spheres have clear, dark contact areas (not washed white)
2. No visible brightness pulsing during slow camera orbits
3. Gold/metallic spheres maintain consistent hue across view angles
4. No bright white halos under objects from SSGI sky bleeding
5. Overall rendering looks sharper and more defined
6. No performance regression > 10% from baseline (target: 200+ FPS)
