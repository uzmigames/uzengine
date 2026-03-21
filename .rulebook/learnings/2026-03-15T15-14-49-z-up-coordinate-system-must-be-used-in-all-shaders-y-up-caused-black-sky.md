# Z-up coordinate system must be used in all shaders — Y-up caused black sky
**Source**: manual
**Date**: 2026-03-15
**Tags**: z-up, coordinate-system, shader, atmosphere, rendering
UzEngine is Z-up (X-forward, Y-right, Z-up). The sky atmosphere shader and transmittance LUT compute shader were using Y-up offsets (`float3(0, bottomRadiusKm, 0)` instead of `float3(0, 0, bottomRadiusKm)`), causing the camera to be offset sideways relative to the planet instead of upward. This made the ray march integrate through the planet body instead of through the atmosphere, producing near-zero luminance (black sky).

Fix: swap Y and Z in all planet-relative position calculations in shaders.

Prevention: when porting shader code from external sources (Bruneton, Hillaire papers, shadertoy), always check and convert coordinate system from Y-up to Z-up.