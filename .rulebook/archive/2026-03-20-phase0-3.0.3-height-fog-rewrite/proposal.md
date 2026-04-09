# Proposal: Height Fog Complete Rewrite (Phase 0)

## Why
The current height fog implementation has chronic bugs that make the scene unusable:
1. Ground disappears at distance — fog absorbs all light at medium-far range
2. Horizon band is black instead of sky-colored haze
3. Inscattering/absorption balance wrong — fog darkens instead of adding atmospheric haze
4. heightFalloff was 20x wrong (0.01 vs UE5's 0.2) — root cause of scene-wide darkness
These bugs persisted across multiple fix attempts because the implementation was built incrementally with guesses. The only fix is a complete rewrite from UE5 HeightFogCommon.ush.

## What Changes

### Delete (910 lines total)
1. `shaders/runtime/atmosphere/height_fog_ps.hlsl` (289 lines)
2. `engine/renderer/fog/fog_renderer.cpp` (280 lines)
3. `engine/renderer/fog/fog_renderer.h` (59 lines)
4. `engine/renderer/passes/atmosphere/height_fog_pass.cpp` (112 lines)
5. `engine/renderer/passes/atmosphere/height_fog_pass.h` (65 lines)

### Rewrite from UE5 source
1. `height_fog_ps.hlsl` — from HeightFogCommon.ush (538 lines) + HeightFogPixelShader.usf (138 lines)
2. `fog_renderer.cpp` — from FogRendering.cpp InitFogConstants (657 lines)
3. `height_fog_pass.cpp` — pass registration (reuse pattern)

## Impact
- Affected specs: `docs/specs/rendering/renderer.md`
- Affected code: `runtime/engine/renderer/fog/`, `runtime/shaders/runtime/atmosphere/`
- Breaking change: NO — same component API, same pass slot
- User benefit: Correct atmospheric haze matching UE5 Basic scene
