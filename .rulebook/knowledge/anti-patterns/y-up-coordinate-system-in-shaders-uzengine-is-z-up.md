# Y-up coordinate system in shaders — UzEngine is Z-up

**Category**: rendering
**Tags**: z-up, coordinate-system, shader, atmosphere, sky, rendering, anti-pattern

## Description

UzEngine uses a Z-up coordinate system (X-forward, Y-right, Z-up), matching UE5 convention. All shaders that place objects relative to a planet center or use "up" direction MUST use Z as the vertical axis, NOT Y.

Common mistake: copying atmosphere/sky code from reference implementations (Bruneton, Hillaire, shadertoy) that use Y-up and forgetting to swap axes. This causes the sky integration to produce near-zero luminance because the camera is offset on the wrong axis — the ray march goes sideways through the planet instead of upward through the atmosphere.

Root cause found: sky_atmosphere_ps.hlsl had `float3(0, bottomRadiusKm, 0)` (Y-up) instead of `float3(0, 0, bottomRadiusKm)` (Z-up). transmittance_lut_cs.hlsl had `float3(0, viewHeight, 0)` instead of `float3(0, 0, viewHeight)`.

## Example

```hlsl
// ❌ WRONG — Y-up (common in reference implementations, shadertoy, OpenGL)
float3 cameraSkyKm = cameraPos.xyz * 0.001 + float3(0.0, bottomRadiusKm, 0.0);
float3 pos = float3(0.0, viewHeight, 0.0);
float3 dir = float3(sinTheta, cosTheta, 0.0);

// ✅ CORRECT — Z-up (UzEngine convention)
float3 cameraSkyKm = cameraPos.xyz * 0.001 + float3(0.0, 0.0, bottomRadiusKm);
float3 pos = float3(0.0, 0.0, viewHeight);
float3 dir = float3(sinTheta, 0.0, cosTheta);
```

## When to Use

Any time you write or review shader code that references a planet surface, sky dome, atmosphere, height-based calculations, or "up" direction. Also when porting shader code from external sources (shadertoy, reference papers, other engines with Y-up).

## When NOT to Use

Code that is purely 2D (screen-space UV operations) or uses view-space / clip-space where the convention doesn't matter.
