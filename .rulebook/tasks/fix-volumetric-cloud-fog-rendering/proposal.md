# Proposal: Fix Volumetric Cloud & Fog Rendering

## Why

The current volumetric cloud and fog system produces severe visual artifacts:
- Strong horizontal banding/slicing from insufficient raymarch steps and poor sample distribution
- Visible "stacked pancake" layering from quantized density sampling
- Fog blends incorrectly with the sky (flat screen-space layer instead of volumetric integration)
- Lack of volumetric depth — clouds appear as flat 2D layers
- No temporal accumulation to amortize ray cost across frames

These issues stem from fundamental algorithmic problems in the raymarch loop, noise sampling,
fog integration, and the absence of temporal reprojection. The fix requires rewriting the
cloud shader's raymarch distribution, adding blue-noise jitter, implementing proper volumetric
fog integration (not a separate flat pass), and adding temporal accumulation with reprojection.

## What Changes

### Shader Changes
1. **clouds.ps.hlsl** — Complete rewrite of raymarch loop:
   - Adaptive step count (64-128 steps based on quality level, up from 32)
   - Blue noise dithering (R2 sequence or spatial blue noise) instead of simple IGN jitter
   - Exponential step distribution (finer steps near camera, coarser far away)
   - Proper Beer-Lambert transmittance integration per step
   - Improved FBM with 4-5 octaves (up from 3) and Worley noise for cloud shapes
   - Height-based density with smooth boundary falloff (cosine, not hard smoothstep)
   - In-scatter energy conservation fix

2. **volumetric_apply.ps.hlsl** — Rewrite fog as integrated volumetric pass:
   - Fog raymarch integrated with cloud pass (not a separate flat screen-space layer)
   - Exponential height fog with proper analytical integration along the view ray
   - Distance-based scattering with Rayleigh/Mie phase functions
   - Smooth blending with sky at depth boundaries

3. **New: clouds_temporal.ps.hlsl** — Temporal accumulation pass:
   - Reproject previous frame's cloud result using motion vectors
   - Blend 90-95% previous frame with 5-10% current frame
   - Neighborhood clamping to prevent ghosting
   - Allows reducing per-frame step count while maintaining smooth appearance

4. **New: clouds_upsample.ps.hlsl** — Optional half-res rendering with bilateral upsample:
   - Render clouds at half resolution for performance
   - Edge-aware bilateral upsample using depth

### C++ Changes
5. **renderer.h/cpp** — New quality levels and temporal state:
   - Quality enum: LOW (32 steps), MEDIUM (64), HIGH (96), CINEMATIC (128)
   - Temporal accumulation buffer (previous frame cloud result + history valid flag)
   - Fog integration moved into cloud pass (remove separate volumetric_apply dispatch)
   - Half-res cloud render target (optional, for LOW/MEDIUM quality)
   - Debug visualization modes (density field, step count heatmap, transmittance)

6. **Scene configuration** — New .uzscene parameters:
   - `cloud_quality` — LOW/MEDIUM/HIGH/CINEMATIC
   - `cloud_temporal_enabled` — enable/disable temporal accumulation
   - Debug toggles

### Test Scene
7. **volumetric_test.uzscene** — Dedicated test scene:
   - Ground plane, sky dome, directional light, camera
   - Cloud layer with fog enabled
   - Multiple camera positions for visual validation

## Impact

- **Affected specs**: rendering pipeline (new temporal pass, modified cloud pass ordering)
- **Breaking changes**: Cloud visual appearance will change significantly (improvement)
- **Performance**: Temporal accumulation allows same or better quality at similar cost
  - Without temporal: ~1.5-2x cost increase (more steps per ray)
  - With temporal: similar or lower cost (amortized across frames, half-res option)
- **User benefit**: Clouds that look like real volumetric clouds, not banded pancakes
