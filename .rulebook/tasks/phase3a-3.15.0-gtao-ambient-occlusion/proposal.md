# Ground Truth Ambient Occlusion (GTAO)

## Summary

Implement GTAO (XeGTAO-based) matching UE5's Screen-Space Ambient Occlusion. GTAO computes per-pixel occlusion by searching depth buffer horizons in screen-space, darkening crevices, corners, and contact regions where ambient light is blocked. Produces the "grounded" look that makes objects feel like they sit in the world rather than float.

## Motivation

Without AO, scenes look flat — objects appear to float because there's no contact darkening. GTAO is the industry standard (used by UE5, Frostbite, id Tech) because it's mathematically correct (integrates visibility over the hemisphere), temporally stable, and fast at half-resolution.

## UE5 Reference Implementation

### Algorithm (UE5 GTAO / ScreenSpaceAmbientOcclusion.usf)
- **Horizon search**: For each pixel, cast 2-8 rays in screen space at different angles (slice directions). For each slice, march through depth buffer finding the maximum horizon angle.
- **Visibility integral**: Integrate the unoccluded solid angle between the two horizon angles and the surface normal → AO value in [0,1].
- **Half-resolution**: Compute AO at half-res for performance, then bilateral upsample using depth/normal edges.
- **Temporal filtering**: Blend AO with previous frame's AO using velocity-based reprojection (reuses TAA velocity buffer).
- **Spatial denoising**: 4×4 bilateral blur weighted by depth similarity — preserves edges while smoothing noise.
- **Multi-bounce approximation**: UE5 approximates indirect bounce by `ao_multibounce = ao / (1 - albedo * (1 - ao))` for colored AO.

### UE5 Parameters (FScreenSpaceAmbientOcclusionParams)
| Parameter | UE5 Default | Description |
|-----------|-------------|-------------|
| `r.AmbientOcclusion.Method` | 1 (GTAO) | 0=SSAO legacy, 1=GTAO |
| `AmbientOcclusionIntensity` | 0.5 | AO darkening multiplier (PostProcessVolume) |
| `AmbientOcclusionRadius` | 200.0 | World-space search radius in cm (UE units) |
| `AmbientOcclusionBias` | 3.0 | Depth bias to prevent self-occlusion |
| `AmbientOcclusionQuality` | 50.0 | 0-100, controls sample count (50=6 slices, 100=8) |
| `AmbientOcclusionMipBlend` | 0.6 | How much to use mip-mapped depth (wider search) |
| `AmbientOcclusionPower` | 2.0 | Exponent applied to final AO (artistic control) |
| `AmbientOcclusionFadeDistance` | 8000.0 | Distance at which AO fades to 1.0 (no occlusion) |
| `AmbientOcclusionFadeRadius` | 5000.0 | Fade transition width |
| Slices | 2 (Low) / 6 (Medium) / 8 (High) | Number of angular search directions |
| Steps per slice | 3-12 | Depth samples per direction |
| Temporal accumulation | Yes | Blends with previous frame AO |

### UE5 Pipeline Position
GTAO runs early in post-processing, before lighting composite. AO texture is multiplied into indirect (ambient) lighting only — does NOT darken direct light/shadows.

### XeGTAO Reference
Intel's XeGTAO is the open-source reference: horizon-based AO with cos-weighted visibility integral, half-res + temporal + bilateral blur. UE5's GTAO is functionally equivalent.

## UzEngine Implementation Plan

### Shader Files
- `gtao_main_cs.hlsl` — Compute shader: horizon search + visibility integral (half-res)
- `gtao_denoise_cs.hlsl` — Compute shader: bilateral spatial filter (4×4 kernel)
- `gtao_upsample_ps.hlsl` — Pixel shader: bilateral depth-aware upsampling to full-res
- `gtao_temporal_cs.hlsl` — Compute shader: temporal accumulation using velocity buffer

### Constant Buffer (register b2)
```hlsl
cbuffer GTAOParams : register(b2) {
    float  aoRadius;            // World-space radius (200.0 UE5 default → convert to view-space)
    float  aoIntensity;         // Multiplier (0.5 UE5 default)
    float  aoPower;             // Exponent (2.0 UE5 default)
    float  aoBias;              // Depth bias (3.0 UE5 default)
    float  aoFadeStart;         // Distance fade start (8000 cm UE5)
    float  aoFadeEnd;           // Distance fade end (13000 cm UE5)
    float2 texelSize;           // Half-res texel size
    float2 fullResTexelSize;    // Full-res texel size
    int    sliceCount;          // 2/6/8 based on quality
    int    stepsPerSlice;       // 3-12 based on quality
    float4x4 projection;       // For view-space depth reconstruction
};
```

### Resources
- **AO half-res**: R8_UNORM, half scene resolution
- **AO full-res**: R8_UNORM, full resolution (upsampled output)
- **AO history**: R8_UNORM for temporal accumulation
- **Depth mips**: Depth buffer with mip chain for hierarchical search

### Quality Levels (match UE5)
- **Low**: 2 slices, 3 steps, no temporal, half-res → ~0.15ms
- **Medium**: 6 slices, 6 steps, temporal, half-res → ~0.3ms
- **High**: 8 slices, 12 steps, temporal, half-res + bilateral upsample → ~0.5ms

## Impact

- **Shaders**: 4 new HLSL files (main, denoise, upsample, temporal)
- **Render graph**: New GTAO pass before lighting composite
- **Textures**: 2× R8_UNORM (half + full res) + 1× R8_UNORM history
- **VRAM**: ~3MB at 1080p
- **Performance**: 0.15-0.5ms depending on quality
- **Dependencies**: Depth buffer, normals (G-Buffer), velocity buffer (for temporal)

## Breaking Changes

None — GTAO is additive. Applied only to ambient/indirect lighting.

## Acceptance Criteria

1. Corners and crevices show visible darkening (contact shadows at object bases)
2. AO intensity matches UE5 default look at intensity=0.5, radius=200cm
3. No self-occlusion artifacts (halos around objects)
4. Half-res computation with bilateral upsample shows no visible block artifacts
5. Temporal filtering reduces noise without introducing ghosting
6. AO fades to 1.0 at configured distance (no AO on distant geometry)
7. Demo screenshot: scene with/without GTAO showing clear quality improvement
