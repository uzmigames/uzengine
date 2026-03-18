<!-- doc-meta
title: Contact Shadows
category: rendering
created: 2026-03-17
-->

# Contact Shadows

## Overview

Contact shadows are screen-space ray-marched shadows that add fine detail at object contact points where cascaded shadow maps (CSM) lack resolution. The technique marches a ray through the depth buffer along the light direction and tests for occlusion.

This implementation follows UE5's `ScreenSpaceShadowRayCast.ush` and `DeferredLightingCommon.ush` contact shadow pipeline.

## Algorithm

1. For each lit pixel in the deferred lighting pass, project a ray from the surface position along the light direction into clip space.
2. Step along the ray in NDC space (8 steps, UE5 default).
3. At each step, sample the depth buffer and compare against the ray's depth.
4. If the ray depth is behind scene geometry (within a tolerance), a hit is detected and the pixel is shadowed.
5. Temporal dithering (Interleaved Gradient Noise) offsets the ray start per-pixel per-frame to reduce banding.

## Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `contactShadowLength` | float | 0.0 | Screen-space ray length fraction. 0 = disabled. Typical: 0.02 (2% of screen depth). |

The parameter is in `PerFrameCB` / `PerFrameParams` and maps directly to the HLSL `contactShadowLength` field.

## UE5 Source References

| UzEngine | UE5 File | UE5 Line | UE5 Function |
|----------|----------|----------|--------------|
| `contact_shadows.hlsli:CastContactShadowRay` | `ScreenSpaceShadowRayCast.ush` | 9-64 | `CastScreenSpaceShadowRay` |
| `contact_shadows.hlsli:ComputeContactShadow` | `DeferredLightingCommon.ush` | 156-229 | `ApplyContactShadowWithShadowTerms` |
| `contact_shadows.hlsli:InterleavedGradientNoise` | `RandomInterleavedGradientNoise.ush` | 11-18 | `InterleavedGradientNoise` |
| `constant_buffers.h:contactShadowLength` | `LightRendering.cpp` | 640 | `ContactShadowLength = 0` |

## UE5 Constants

| Constant | Value | Source |
|----------|-------|--------|
| `CompareToleranceScale` | 2.0 | `DeferredLightingCommon.ush:68` |
| `NumSteps` | 8 | `DeferredLightingCommon.ush:197`, `ScreenSpaceShadows.usf:143` |
| `ContactShadowCastingIntensity` | 1.0 | `LightRendering.cpp:641` |
| `ContactShadowNonCastingIntensity` | 0.0 | `LightRendering.cpp:642` |
| IGN magic values | (0.06711056, 0.00583715, 52.9829189) | `RandomInterleavedGradientNoise.ush:16` |
| IGN frame jitter | `FrameId * float2(47, 17) * 0.695` | `RandomInterleavedGradientNoise.ush:14` |

## Deviations from UE5

1. **No per-material CastContactShadow flag**: UE5 checks a G-Buffer stencil bit to skip certain materials. UzEngine applies contact shadows uniformly.
2. **No per-shading-model overrides**: UE5 forces contact shadows for Hair (0.2) and Eye (0.5 WS) materials. UzEngine does not have these shading models yet.
3. **No subsurface exponential attenuation**: UE5 reduces contact shadow intensity on subsurface materials using `exp(-Density * HitDistance)`. UzEngine skips this until SSS is implemented.
4. **Simplified screen-to-world scaling**: UE5 uses `GetScreenRayLengthMultiplierForProjectionType` (precomputed in view buffer). UzEngine uses `contactShadowLength * viewDepth` which is equivalent for perspective projection.

## Usage

To enable contact shadows, set `contactShadowLength` on `PerFrameCB`:

```cpp
perFrame.contactShadowLength = 0.02f; // 2% of screen depth
```

## Performance

- 8 depth buffer samples per pixel per light (with contact shadows enabled).
- Cost is proportional to pixel count. At 1080p, approximately 0.1-0.3ms depending on GPU.
- Disabled by default (0.0) for zero overhead.

## Files

- `runtime/shaders/runtime/include/contact_shadows.hlsli` -- ray march and noise functions
- `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl` -- integration point
- `runtime/engine/renderer/core/params/constant_buffers.h` -- `contactShadowLength` field
- `runtime/engine/renderer/core/params/per_frame_params.h` -- `contactShadowLength` field
