# Proposal: F82 Extended Fresnel (Phase 2)

## Why
Standard Schlick Fresnel only uses F0 (reflectance at normal incidence), but real metals have a dip at ~82 degrees that Schlick does not capture. The Adobe Standard Material F82 extension (adopted in UE5 5.6 BRDF.ush:391) adds an F82 parameter to control reflectance at 82 degrees, improving accuracy for metals like copper and gold at mid-angles. This is a trivial 0.5 day addition — one function alongside existing F_Schlick.

## What Changes

### New Systems
1. `F_AdobeF82(float3 F0, float3 F82, float VoH)` — Extended Fresnel function in brdf.hlsli

### Refactoring
- brdf.hlsli: add F_AdobeF82 alongside F_Schlick, selected per-material when F82 is non-default
- PBRMaterial: add optional `float3 f82Tint = {1,1,1}` (default matches standard Schlick behavior)

## Impact
- Affected specs: docs/specs/rendering/shader-library.md
- Affected code: runtime/shaders/runtime/include/brdf.hlsli, runtime/engine/renderer/lighting/brdf_math.h
- Breaking change: NO — additive, default F82=(1,1,1) produces identical result to Schlick
- User benefit: More accurate mid-angle reflectance for metals
- Enables: Adobe Standard Material compatibility
