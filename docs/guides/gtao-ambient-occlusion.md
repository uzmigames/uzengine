# GTAO Ambient Occlusion

<!-- doc-meta
title: GTAO Ambient Occlusion
category: guide
engine: uzengine
theme: rendering
created: 2026-03-17
updated: 2026-03-17
status: active
author: docs-operator
-->

## Overview

GTAO (Ground Truth Ambient Occlusion) is a horizon-based ambient occlusion technique that darkens concave geometry to simulate indirect shadow falloff. Unlike screen-space (HBAO+, SSAO), GTAO evaluates at full geometric scale using the G-Buffer normal and depth. A compute shader dispatches two passes per frame: raw AO computation followed by bilateral denoise for clean results.

This system achieves UE5 parity with `PostProcessAmbientOcclusion.usf` and is part of the PBR rendering milestone.

## Algorithm Overview

### Horizon-Based Occlusion (Jimenez 2016)

GTAO samples occlusion via horizon angle queries around each pixel:

```
For each pixel:
  N = sample normal from GBuffer
  Z = sample depth
  viewPos = screen-to-view(pixel, Z)

  For each of 2 azimuthal directions:
    For each of 4 radial taps:
      tapPos = viewPos + radius * direction + distance * tapVec
      tapDepth = sample(depthBuffer, tapPos)
      horizonAngle = angle between N and (tapPos - viewPos).normalized()

  AO = 1 - average(sin(horizonAngle))  // 0=fully occluded, 1=unoccluded
```

The horizon angle is the maximum elevation angle from the surface normal to any point on the screen at a given azimuth and radial distance. Points above the horizon indicate occlusion.

**Source**: UE5 `PostProcessAmbientOcclusion.usf` — GTAO algorithm.

### Temporal Jitter and Anti-Aliasing

To avoid banding artifacts from discrete tap directions, GTAO applies temporal jitter via an Interleaved Gradient Noise (IGN) pattern:

```hlsl
float jitter = InterleavedGradientNoise(pixelCoord, frameIndex);
azimuthOffset = jitter * 2 * PI;  // Rotate tap directions each frame
```

Over 4+ frames, tap directions converge to a smoother appearance. A bilateral denoise (see below) further smooths and removes firefly artifacts.

**Source**: UE5 `PostProcessAmbientOcclusion.usf:553-557` — Temporal jitter pattern.

### AO Power Curve

The raw AO value is raised to a power curve for artistic control:

```hlsl
aoOutput = pow(1 - rawAO, aoPower)  // Default aoPower = 2.0
```

Higher power (e.g., 3.0) darkens occlusion more aggressively; lower power (e.g., 1.0) produces subtle falloff.

**Source**: UE5 `Scene.cpp:528` — `AmbientOcclusionPower = 2.0f`.

### Multi-Bounce Colored AO

To prevent over-darkening of colored surfaces under occlusion, a polynomial approximation (Jimenez 2016) is applied during shading:

```hlsl
// In deferred_lighting_ps.hlsl, after AO is sampled:
float3 AOMultiBounce(float3 baseColor, float ao) {
    float3 a = 2.0404 * baseColor - 0.3324;
    float3 b = -4.7951 * baseColor + 0.6417;
    float3 c = 1.7706 * baseColor - 0.0259;
    return c * pow(ao, (a * ao + b));  // Colored occlusion falloff
}
```

This approximates energy conservation for bounced light on colored surfaces.

**Source**: UE5 `DeferredShadingCommon.ush:1220` — `AOMultiBounce`.

## Runtime Parameters

All parameters are configurable via `GTAOSettings` and passed to the compute shader:

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| **enabled** | bool | true | — | Master toggle. If false, the shader outputs 1.0 (no occlusion). |
| **worldRadius** | float | 200.0 cm | [50, 1000] | Search radius around each pixel. Larger = broader occlusion influence (slower). UE5 default: 200 cm. |
| **intensity** | float | 1.0 | [0, 2] | AO strength multiplier. Applied after power curve: `outputAO = pow(rawAO, aoPower) * intensity`. |
| **thicknessBlend** | float | 0.5 | [0, 1] | Thickness heuristic for horizon occlusion. Controls how aggressively thin geometry (leaves, grass) occludes. |
| **fadeStartDist** | float | 50000 cm | [1000, 100000] | Distance where AO begins to fade (in cm). |
| **fadeEndDist** | float | 100000 cm | [fadeStart, 200000] | Distance where AO is fully transparent. Beyond this depth, AO contribution → 0. |
| **aoPower** | float | 2.0 | [0.5, 4.0] | Power curve exponent. Higher = more aggressive darkening. |
| **multiBounce** | bool | true | — | Enable colored multi-bounce approximation (Jimenez 2016). Always active in deferred_lighting_ps.hlsl. |

## GPU Dispatch

### Compute Root Constants (b0 at 8 DWORDs)

Critical per-frame values are passed as root constants to fit within the 64 DWORD limit:

```cpp
struct GTAORootConstants {
    uint32_t depthSrvIndex;      // GBuffer depth SRV
    uint32_t normalSrvIndex;     // GBuffer normal SRV
    uint32_t outputWidth;        // Output texture width
    uint32_t outputHeight;       // Output texture height
    float invTanHalfFov;         // 1 / tan(fovY / 2) for view projection
    float nearPlane;             // Camera near plane (cm)
    float farPlane;              // Camera far plane (cm)
    uint32_t frameIndex;         // For temporal jitter (IGN pattern)
};
```

**Thread Group**: 8×8×1 = 64 threads per group. For 1920×1080, dispatch (240, 135, 1).

### Constant Buffer (b2, 256 bytes aligned)

Bulk parameters are stored in a D3D12 constant buffer:

```cpp
struct GTAOParamsCBV {
    // Temporal jitter (4 floats)
    float temporalCos, temporalSin, temporalOffset, thicknessBlend;

    // Screen geometry (4 floats)
    float targetSizeX, targetSizeY, invTargetSizeX, invTargetSizeY;

    // Search radius and projection (4 floats)
    float worldRadius, invTanHalfFov, nearPlane, farPlane;

    // Fade and intensity (4 floats)
    float fadeMul, fadeAdd, maxFadeDepth, aoIntensity;

    // View matrix (16 floats)
    float viewMatrix[16];

    // Padding to 256 bytes (32 floats)
    float _pad[32];
};
```

### Denoise Pass (Second Compute Dispatch)

After raw AO, a bilateral denoise pass filters the result to remove temporal noise and fireflies:

```cpp
struct GTAODenoiseRootConstants {
    float texelSizeX, texelSizeY;
    float nearPlane, farPlane;
    uint32_t rawAOSrvIndex;      // Input: raw AO texture
    uint32_t depthSrvIndex;      // GBuffer depth
    uint32_t _pad[2];
};
```

The denoise kernel is a separable bilateral filter that preserves edges (using depth discontinuities) while smoothing flat regions.

## Integration with Deferred Lighting

GTAO is executed **after** the GBuffer pass and **before** the deferred lighting pass:

```
GBuffer Pass (depth, normal, albedo, etc.)
    ↓
[GTAO Compute] ← Reads GBuffer depth & normal
    ↓
[GTAO Denoise Compute] ← Bilateral filter
    ↓
Deferred Lighting Pass ← Samples filtered AO via PerFrameCB::gtaoSrvIndex
    ↓
Post-Processing (Bloom, Tonemap, etc.)
```

The filtered AO SRV index is stored in `PerFrameCB::gtaoSrvIndex` for the deferred lighting shader to sample.

In the deferred lighting shader, AO modulates the ambient term:

```hlsl
float3 ao = gtaoTexture.Load(pixelCoord).rrr;  // Fetch AO
float3 ambientLight = EstimateAmbient(normal);
return ambientLight * ao * AOMultiBounce(baseColor, ao);
```

## Quality vs Performance

GTAO is fill-rate bound on modern GPUs. Cost scaling:

| Resolution | Quality | Approximate GPU Time | Use Case |
|------------|---------|----------------------|----------|
| 1920×1080 Full-Res | High (64 taps) | ~2.5 ms | High-end (RTX 3080+) |
| 1920×1080 Full-Res | Medium (32 taps) | ~1.5 ms | Mainstream (RTX 3070) |
| 1280×720 Full-Res | High (64 taps) | ~1.0 ms | Console, low-end |

**Optimization Tips**:
- Disable GTAO if world radius < 50 cm (minimal occlusion influence).
- Use half-resolution AO + bilateral upsample for 4× speedup on quality-constrained budgets.
- Tune `intensity` down to 0.7 for subtle, realistic falloff.

## Artist Tuning Guide

### Default (UE5 High Quality)
```
worldRadius: 200.0 cm
intensity: 1.0
thicknessBlend: 0.5
fadeStart: 50000 cm (500 m)
fadeEnd: 100000 cm (1000 m)
aoPower: 2.0
```
Balanced occlusion coverage; good for outdoor scenes with mixed geometry.

### Indoor/Tight Spaces
```
worldRadius: 100.0 cm (tighter falloff)
intensity: 1.3
thicknessBlend: 0.3 (thinner geometry occludes less)
aoPower: 2.5
```
Emphasizes crevices and corners; less ambient influence at distance.

### Subtle Falloff
```
worldRadius: 300.0 cm (broader influence)
intensity: 0.6
aoPower: 1.5
```
Soft, nearly imperceptible occlusion; useful for bright, outdoor scenes.

## Key Takeaways

- **Horizon-Based**: GTAO evaluates occlusion via screen-space horizon angles, not ray tracing.
- **Temporal Jitter**: IGN-based per-frame offset converges to smooth results over 4+ frames.
- **AO Power Curve**: Artist control via `aoPower` exponent (default 2.0) darkens or softens falloff.
- **Multi-Bounce**: Colored AO prevents over-darkening on colored surfaces (Jimenez 2016 fit).
- **Bilateral Denoise**: Separable filter removes fireflies and temporal noise while preserving edges.
- **Compute-Based**: Two dispatches (raw + denoise); 8×8 thread groups; ~1.5 ms full-res on mainstream GPU.

## References

- UE5 `PostProcessAmbientOcclusion.usf` — GTAO algorithm and shader
- UE5 `PostProcessAmbientOcclusionCommon.ush` — Shared GTAO helper functions
- UE5 `DeferredShadingCommon.ush:1220` — AOMultiBounce colored occlusion
- UE5 `Scene.cpp:528` — AmbientOcclusionPower default (2.0f)
- Jimenez 2016 — "Ground Truth Ambient Occlusion" (SIGGRAPH course)
- [GTAO Pass Header](../../runtime/engine/renderer/passes/postprocess/gtao_pass.h)

## Changelog

| Date | Change |
|------|--------|
| 2026-03-17 | Initial creation — algorithm, parameters, integration, tuning guide |
