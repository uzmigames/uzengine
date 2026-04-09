# Film Grain

## Summary

Implement film grain matching UE5's Film Grain system. Adds procedural noise simulating analog film stock grain, applied after tone mapping in SDR space. Uses blue-noise temporal dithering for perceptually smooth grain that doesn't strobe.

## UE5 Reference Implementation

### Algorithm (UE5 FilmGrain.usf)
- **Blue-noise texture**: 128×128 blue-noise tiled across screen. Blue noise is perceptually smoother than white noise — no visible clumps.
- **Temporal variation**: Each frame uses different blue-noise offset (`frame_index * golden_ratio`), preventing static grain pattern.
- **Luminance-dependent**: Grain is stronger in midtones, weaker in shadows (film grain is exposure-dependent). UE5: `grainStrength *= smoothstep(0.0, 0.5, luminance) * smoothstep(1.0, 0.8, luminance)`.
- **Per-channel**: Slight chromatic grain — R/G/B channels get slightly different noise offsets for color grain effect.

### UE5 Parameters
| Parameter | UE5 Default | Description |
|-----------|-------------|-------------|
| `FilmGrainIntensity` | 0.0 | Grain strength (0-1, disabled by default) |
| `FilmGrainIntensityShadows` | 0.5 | Grain in shadows relative to midtones |
| `FilmGrainIntensityMidtones` | 1.0 | Grain in midtones (reference) |
| `FilmGrainIntensityHighlights` | 0.3 | Grain in highlights relative to midtones |
| `FilmGrainShadowsMax` | 0.09 | Shadow luminance threshold |
| `FilmGrainHighlightsMin` | 0.5 | Highlight luminance threshold |
| `FilmGrainHighlightsMax` | 1.0 | Highlight luminance max |
| `FilmGrainTexelSize` | 1.0 | Grain texel scale (0.5=fine, 2.0=coarse) |
| `FilmGrainTexture` | default | Blue-noise source texture |

## Impact

- **Shaders**: 1 shader (can be combined with final composite pass)
- **Textures**: 1× 128×128 R8 blue-noise texture (16KB)
- **Performance**: ~0.02ms (texture sample + add)
- **VRAM**: 16KB

## Acceptance Criteria

1. Grain visible at intensity=0.5, not visible at intensity=0.0
2. Grain is temporally varying (not static pattern)
3. Midtones have more grain than shadows/highlights
4. No visible tiling pattern from blue-noise texture
5. Demo: film grain ON/OFF comparison, intensity slider
