# Analytical fallbacks always produce visibly wrong results
**Source**: manual
**Date**: 2026-03-16
**Tags**: ue5-parity, rendering, critical-learning, anti-pattern
Every time an "analytical approximation" or "estimated fallback" was used instead of reading UE5 source code, the visual result was obviously wrong:

1. SH irradiance analytical fallback with ratios (0.7, 0.8, 1.0) — produced overly blue ground instead of neutral grey
2. skyLightIntensity = 1.5 "to compensate for analytical SH" — overwashed the scene
3. min clamp on aerial perspective skyInscatter = max(val, (0.15, 0.2, 0.35)) — forced blue on everything
4. Fog with baseHeight=0 and invented density/falloff — covered entire scene in blue wash
5. Shadow texel snapping applied to projection matrix instead of final VP — displaced entire shadow map out of NDC range

Root cause: Claude's model knowledge about rendering math is approximate. UE5 source code is exact. The delta between "approximately correct" and "exactly correct" is VERY visible in rendering.

Fix: ALWAYS read UE5 source first. NEVER invent values. If a GPU feature fails (cubemap SH readback returns zeros), fix the GPU feature instead of inventing a CPU fallback.