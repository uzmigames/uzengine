# Proposal: Stephen Hill ACES Fit + Specular Occlusion (Phase 2)

## Why
UzEngine uses the Narkowicz 2015 single-curve ACES fit which loses hue in saturated highlights (orange shifts to yellow). Stephen Hill's 2016 fitted RRT+ODT matrices preserve hue significantly better and are the standard in UE5 and Falcor. Additionally, IBL specular light leaks through occluded crevices because AO only affects diffuse — specular occlusion (Lagarde 2014 / Filament model) is a single-line formula that dramatically improves indoor and cavity scenes. Both fixes combined take less than 2 days for a visible quality jump across every rendered frame.

## What Changes

### New Systems
1. `AcesHillFit()` — Replace Narkowicz ACES body with Hill's 3x3 input/output matrix RRT+ODT fitted curve in tonemap_ps.hlsl
2. `ComputeSpecularOcclusion()` — Add Lagarde/Filament specular occlusion formula to ibl.hlsli, applied during IBL specular evaluation

### Refactoring
- tonemap_ps.hlsl: replace `AcesFilm()` body with Hill matrices (same function signature, drop-in replacement)
- ibl.hlsli: multiply IBL specular result by specular occlusion term before adding to output
- brdf_math.h: add C++ mirror of both functions for Tier 1 parity tests

## Impact
- Affected specs: docs/specs/rendering/shader-library.md
- Affected code: runtime/shaders/runtime/postprocess/tonemap_ps.hlsl, runtime/shaders/runtime/include/ibl.hlsli, runtime/engine/renderer/lighting/brdf_math.h
- Breaking change: NO — visual improvement only, same API
- User benefit: Better hue preservation in highlights, no specular light leaking in crevices
- Enables: Closer Falcor reference parity (PSNR improvement)
