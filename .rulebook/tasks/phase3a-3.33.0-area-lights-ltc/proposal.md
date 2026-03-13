# Area Lights (Linearly Transformed Cosines)

## Priority: P3 — Advanced lighting realism

## Summary

Implement rectangular area lights using Linearly Transformed Cosines (LTC) matching UE5's RectLight. Area lights produce soft, realistic illumination with physically correct falloff — unlike point lights which are infinitely small and produce infinitely sharp specular highlights. LTC analytically integrates the BRDF over the solid angle of the light shape.

## UE5 Reference

### Algorithm (RectLight in DeferredLightingCommon.ush)
- **LTC**: Precomputed 2 textures (64×64 each): LTC amplitude matrix + Fresnel norm
- **Runtime**: Transform BRDF distribution into cosine distribution via 3×3 matrix lookup (roughness × NdotV)
- **Integration**: Analytically integrate cosine over clipped polygon (light rect projected to hemisphere)
- **Diffuse**: Same LTC framework but with identity matrix (Lambertian = cosine distribution)

### UE5 Rect Light Parameters
| Parameter | UE5 Default | Description |
|-----------|-------------|-------------|
| `SourceWidth` | 64.0 | Width in cm |
| `SourceHeight` | 64.0 | Height in cm |
| `BarnDoorAngle` | 88° | Barn door cutoff angle |
| `BarnDoorLength` | 20.0 | Barn door length |
| `AttenuationRadius` | 1000.0 | Light range |
| `Intensity` | 5.0 cd | Candela |
| `LightColor` | White | Color tint |

### LTC Precomputed Textures (Real-Time Polygonal-Light Shading with LTC, Heitz 2016)
- **LTC Matrix** (64×64 RGBA32F): M⁻¹ inverse transformation matrix per (roughness, NdotV)
- **LTC Amplitude** (64×64 RG32F): Fresnel scale + norm per (roughness, NdotV)
- Total: ~100KB precomputed data

## Impact

- **Shaders**: LTC evaluation include (~100 lines), rect light polygon clipping
- **Textures**: 2× 64×64 precomputed LTC LUTs (~100KB)
- **Light types**: New LightType=3 (RectArea) in LightGPU struct
- **Performance**: ~0.15ms per area light (analytic, no sampling)

## Acceptance Criteria

1. Rectangular area light produces soft illumination with visible light shape reflection on glossy surfaces
2. Specular highlight matches light rectangle shape (not point-like)
3. Smooth roughness transition: mirror → blurred rect → diffuse
4. Attenuation and falloff match UE5 RectLight behavior
5. Demo: glossy floor reflecting rectangular area light showing rect-shaped highlight
