# Temporal Anti-Aliasing (TAA)

## Summary

Implement Temporal Anti-Aliasing matching UE5's TemporalAA pass. TAA resolves sub-pixel aliasing by jittering the camera projection each frame and blending with reprojected history, producing smooth edges equivalent to 4-8x MSAA at a fraction of the cost. FXAA (3.5.0) becomes fallback when TAA is disabled.

## Motivation

FXAA blurs edges indiscriminately and cannot resolve sub-pixel detail (thin wires, specular highlights). TAA accumulates sub-pixel samples over time, resolving detail that single-frame AA cannot. Every modern AAA engine (UE5, Frostbite, DOOM Eternal) uses TAA as primary AA. It's also a prerequisite for stable SSAO, SSR, and volumetric effects.

## UE5 Reference Implementation

### Algorithm (UE5 TemporalAA.usf)
- **Jitter**: Halton(2,3) sequence, 8 samples cycling, applied as sub-pixel offset to projection matrix
- **Velocity buffer**: Per-pixel motion vectors from depth reprojection (camera) + per-object velocity (skeletal/dynamic)
- **History reprojection**: Sample previous frame at `currentUV - velocity`, bilinear filtered
- **Neighborhood clamping**: Convert to **YCoCg** color space, compute AABB from 3×3 neighborhood of current frame, clamp history to this box (prevents ghosting)
- **Blend factor**: `lerp(history, current, 1.0/N)` where N=8 typical. Reduced when velocity is high (fast motion → more current frame)
- **Responsive AA**: UE5 marks pixels as "responsive" (UI elements, hair) with stencil bit — these use faster convergence (N=4)
- **Sharpening**: Post-TAA sharpen pass to counteract temporal blur (contrast-adaptive sharpening)

### UE5 Parameters (FTemporalAAHistory)
| Parameter | UE5 Default | Description |
|-----------|-------------|-------------|
| `r.TemporalAA.Quality` | 2 (High) | 0=Off, 1=Low (faster), 2=High (full neighborhood) |
| `r.TemporalAA.Upsampling` | 0 | 1=enable temporal upsampling (render at lower res) |
| `r.TemporalAA.FilterSize` | 1.0 | Jitter filter width (Blackman-Harris kernel) |
| `r.TemporalAA.CurrentFrameWeight` | 0.04 | Weight of current frame vs history (1/N) |
| `r.TemporalAA.HistoryScreenPercentage` | 100 | History buffer resolution % |
| Jitter sequence | Halton(2,3) | 8-sample quasi-random sub-pixel offsets |
| Color space | YCoCg | Neighborhood clamping in YCoCg for perceptual accuracy |
| History rejection | Velocity + luminance | Reject history when motion vectors are large or color differs |

### UE5 Pipeline Position
After all lighting and post-process (DoF, motion blur), before tone mapping. TAA operates in HDR (linear) space.

## UzEngine Implementation Plan

### Shader Files
- `taa_resolve_ps.hlsl` — Main TAA resolve (jitter compensation, history blend, neighborhood clamp)
- `velocity_ps.hlsl` — Velocity buffer generation from depth reprojection + per-object motion

### Constant Buffer (register b2 — kSlotPerObject)
```hlsl
cbuffer TAAParams : register(b2) {
    float2 jitterOffset;        // Current frame sub-pixel jitter (Halton)
    float2 jitterOffsetPrev;    // Previous frame jitter (for unjitter)
    float  currentFrameWeight;  // 1/N blend factor (0.04 default, UE5 match)
    float  varianceClipGamma;   // Neighborhood clamp tightness (1.0 default)
    float2 texelSize;           // 1/resolution for UV offsets
    float4x4 prevViewProj;     // Previous frame view-projection for reprojection
    float4x4 invViewProj;      // Current frame inverse VP for world-space reproject
};
```

### Resources
- **History texture**: RGBA16F, same resolution as scene, ping-pong double buffer
- **Velocity buffer**: RG16F, written during G-Buffer pass (camera reprojection) or separate pass
- **Depth buffer**: Read-only, from G-Buffer

### Quality Levels (match UE5)
- **Off**: TAA disabled, fall back to FXAA
- **Low**: 4-sample Halton, 3×3 neighborhood, no sharpening
- **High**: 8-sample Halton, 5×5 neighborhood (cross pattern), CAS sharpening post-TAA

## Impact

- **Shaders**: 2 new HLSL files (taa_resolve_ps, velocity_ps)
- **Render graph**: New TAA pass after lighting/DoF/motion blur, before tonemap
- **Textures**: 2× RGBA16F history buffers (ping-pong), 1× RG16F velocity buffer
- **VRAM**: ~24MB at 1080p (2× history + velocity)
- **Performance**: ~0.3ms at 1080p on RTX 4090
- **Dependencies**: Requires depth buffer (G-Buffer pass), projection matrix access

## Breaking Changes

None — TAA is additive. FXAA remains as fallback.

## Acceptance Criteria

1. TAA produces visibly smoother edges than FXAA on thin geometry (wires, railings, foliage)
2. No visible ghosting on fast camera rotation (neighborhood clamping works)
3. Moving objects don't leave trails (velocity buffer reprojection correct)
4. Halton jitter visible as sub-pixel shimmer when TAA is OFF, stable when ON
5. Demo screenshot comparison: FXAA vs TAA side-by-side shows clear quality improvement
6. currentFrameWeight=0.04 matches UE5 default temporal stability
