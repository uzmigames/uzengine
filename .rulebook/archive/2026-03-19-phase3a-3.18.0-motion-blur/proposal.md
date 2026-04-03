# Motion Blur

## Summary

Implement per-pixel motion blur matching UE5's velocity-based motion blur. Uses per-pixel velocity buffer (from camera reprojection + per-object motion vectors) to directionally blur pixels along their motion direction, simulating camera shutter exposure. Produces cinematic motion feel for camera pans and fast-moving objects.

## Motivation

Without motion blur, fast camera movement and object motion look "stroby" — individual frames are frozen snapshots. Motion blur simulates real camera shutter behavior (180° shutter angle = half-frame exposure), adding smoothness and speed perception. Critical for racing games, action cameras, and cinematic cutscenes.

## UE5 Reference Implementation

### Algorithm (UE5 MotionBlur.usf)
- **Velocity buffer**: Per-pixel RG16F motion vectors. Camera contribution = depth-reproject using prev/current ViewProj. Per-object contribution = vertex shader outputs prevWorldPos.
- **Tile max**: Divide screen into tiles (16×16 or 20×20), find max velocity magnitude per tile. Used to skip blur on static tiles.
- **Neighbor max**: Expand tile max to 3×3 neighborhood — ensures a fast-moving object's blur extends into neighboring tiles.
- **Scatter-as-gather**: For each pixel, sample along its velocity vector + neighboring tiles' dominant velocity. This simulates "a fast object's blur covers nearby pixels" without actual scatter.
- **Depth weighting**: Prefer foreground velocity when foreground and background overlap (foreground blur covers background).
- **Velocity scaling**: Scale velocity by `0.5 * targetFPS / currentFPS * shutterAngle / 360`. Default shutter angle = 180° (cinematic).

### UE5 Parameters (FPostProcessSettings — Motion Blur)
| Parameter | UE5 Default | Description |
|-----------|-------------|-------------|
| `MotionBlurAmount` | 0.5 | Blur intensity multiplier (PostProcessVolume) |
| `MotionBlurMax` | 5.0 | Max blur radius in % of screen width |
| `MotionBlurTargetFPS` | 30 | FPS target for velocity scaling (30 = cinematic) |
| `MotionBlurPerObjectSize` | 0.5 | Per-object blur contribution scale |
| `r.MotionBlur.Quality` | 4 | 0=off, 1=low, 2=med, 3=high, 4=very high |
| Shutter angle | 180° | Implied by Amount (180° = standard cinema) |
| Tile size | 20×20 | Tile classification size |
| Samples | 6/8/12/16 | Gather samples along velocity by quality |

### UE5 Pipeline Position
After DoF, before TAA. Motion blur in HDR space.

## UzEngine Implementation Plan

### Shader Files
- `motionblur_tilemax_cs.hlsl` — Compute: find max velocity per 16×16 tile
- `motionblur_neighbormax_cs.hlsl` — Compute: expand tile max to 3×3 neighborhood
- `motionblur_gather_ps.hlsl` — Pixel: scatter-as-gather blur along velocity + neighbor velocity

### Constant Buffer (register b2)
```hlsl
cbuffer MotionBlurParams : register(b2) {
    float  blurAmount;          // 0.5 UE5 default
    float  maxBlurRadius;       // Max blur in pixels (from screen %)
    float  targetFPS;           // 30 UE5 default
    float  shutterAngle;        // 180° default (degrees)
    float  perObjectScale;      // 0.5 UE5 default
    int    sampleCount;         // 6/8/12/16 by quality
    float2 texelSize;
    float  velocityScale;       // Pre-computed: 0.5 * targetFPS/currentFPS * shutter/360
    float3 pad;
};
```

### Quality Levels (match UE5)
- **Low**: 6 samples, no tile classification, camera-only velocity → ~0.15ms
- **Medium**: 8 samples, tile max, camera + object velocity → ~0.3ms
- **High**: 12 samples, tile + neighbor max, depth weighting → ~0.5ms
- **Very High**: 16 samples, full scatter-as-gather with depth priority → ~0.7ms

## Impact

- **Shaders**: 3 new HLSL files
- **Dependencies**: Velocity buffer (shared with TAA), depth buffer
- **Textures**: Tile max (R16F, 1/16 res), neighbor max (R16F)
- **VRAM**: ~1MB at 1080p
- **Performance**: 0.15-0.7ms

## Breaking Changes

None — additive effect.

## Acceptance Criteria

1. Camera pan produces smooth directional blur (not just blurry — directional streaks)
2. Fast-moving object leaves blur trail while static background is sharp
3. No blur on static scenes with static camera
4. targetFPS=30 at 60fps produces half-frame exposure blur (cinematic look)
5. maxBlurRadius clamps extreme velocities (no pixel-soup)
6. Demo screenshot: spinning object + camera pan showing directional blur
