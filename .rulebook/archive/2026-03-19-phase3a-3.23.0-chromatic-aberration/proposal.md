# Chromatic Aberration

## Summary

Implement chromatic aberration matching UE5's SceneFringe system. Simulates real lens chromatic dispersion by offsetting R/G/B channels radially from screen center. Stronger at edges, zero at center — mimics cheap/vintage lens character.

## UE5 Reference Implementation

### Algorithm (UE5 — PostProcessTonemap.usf, SceneFringe)
- **Radial offset**: Compute radial distance from screen center. R channel sampled inward, B channel sampled outward, G stays centered.
- **UV offset**: `offsetR = normalize(uv - center) * -intensity * d²; offsetB = normalize(uv - center) * +intensity * d²`. Quadratic falloff = stronger at edges.
- **3-tap sample**: `color.r = scene.Sample(uv + offsetR).r; color.g = scene.Sample(uv).g; color.b = scene.Sample(uv + offsetB).b;`

### UE5 Parameters
| Parameter | UE5 Default | Description |
|-----------|-------------|-------------|
| `SceneFringeIntensity` | 0.0 | Chromatic aberration strength (0=off, 5.0=extreme) |
| `SceneFringeMaxSize` | 5.0 | Max offset in % of screen (clamp) |
| Start offset | 0.5 | Normalized distance from center where CA begins |

## Impact

- **Shaders**: ~15 lines in final composite (no new file needed)
- **Performance**: ~0.02ms (2 extra texture samples)
- **VRAM**: 0

## Acceptance Criteria

1. Visible RGB fringe at screen edges at intensity=3.0
2. Center of screen has no fringe
3. Intensity=0 produces no effect
4. RGB split follows radial direction (not horizontal/vertical)
5. Demo: CA ON/OFF, intensity slider, zoom on edge showing R/B split
