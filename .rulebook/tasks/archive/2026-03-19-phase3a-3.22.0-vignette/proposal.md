# Vignette

## Summary

Implement vignette matching UE5's Vignette system. Darkens screen edges in a radial gradient from center, simulating real camera lens optical falloff. Simple but effective for focusing attention and adding cinematic mood.

## UE5 Reference Implementation

### Algorithm (UE5 — PostProcessCommon.ush, applied in tonemap)
- **Radial distance**: `d = length((uv - 0.5) * 2.0)` — distance from screen center in [-1,1] range
- **Vignette curve**: `vignette = saturate(1.0 - smoothstep(radius, radius + smoothness, d * intensity))` — smooth falloff
- **Application**: `color *= vignette` — multiplicative darkening
- **Aspect correction**: UV is aspect-ratio corrected so vignette is circular, not elliptical

### UE5 Parameters
| Parameter | UE5 Default | Description |
|-----------|-------------|-------------|
| `VignetteIntensity` | 0.4 | Darkening strength (0=off, 1=fully black at corners) |
| Radius | ~0.6 (internal) | Start of darkening from center |
| Smoothness | ~0.3 (internal) | Transition width |

## Impact

- **Shaders**: ~10 lines added to final composite pass (no new file needed)
- **Performance**: ~0.01ms (math only, no texture)
- **VRAM**: 0

## Acceptance Criteria

1. Screen corners are visibly darker at intensity=0.4
2. Center of screen is unaffected
3. Intensity=0 produces no darkening
4. Vignette is circular (not stretched on widescreen)
5. Demo: toggle vignette ON/OFF, intensity slider
