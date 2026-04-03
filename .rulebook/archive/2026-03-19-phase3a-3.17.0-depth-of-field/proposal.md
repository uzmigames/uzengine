# Depth of Field (DoF)

## Summary

Implement cinematic Depth of Field matching UE5's Cinematic DoF. Uses thin-lens Circle of Confusion (CoC) computation with gather-based bokeh blur to create natural focus/defocus. Supports near-field blur (foreground), far-field blur (background), and configurable bokeh shape.

## Motivation

DoF is the most impactful cinematic effect — it guides the player's eye to the focal point, adds depth perception, and creates film-quality visuals for cutscenes, photo modes, and cinematic cameras. UE5's Cinematic DoF is the reference standard.

## UE5 Reference Implementation

### Algorithm (UE5 DiaphragmDOF / BokehDOF.usf)
- **CoC computation**: Thin-lens equation: `CoC = focalLength² / (aperture * (focusDistance - focalLength)) * abs(depth - focusDistance) / depth`. Positive CoC = far blur, negative = near blur.
- **Tile classification**: Divide screen into 16×16 tiles. Classify each tile as: foreground-only, background-only, or mixed. Only mixed tiles get expensive scatter.
- **Gather pass**: For each out-of-focus pixel, gather from a disk of radius=CoC, weighted by bokeh kernel. UE5 uses **diaphragm simulation** (5-8 blade aperture shape).
- **Near/far separation**: Process near and far blur separately (near needs alpha for compositing over in-focus). Near-field presort by CoC to prevent in-focus bleeding.
- **Recombine**: Composite near-field (with alpha) over in-focus over far-field. Smooth transitions at CoC boundaries.

### UE5 Parameters (FPostProcessSettings — Depth of Field)
| Parameter | UE5 Default | Description |
|-----------|-------------|-------------|
| `DepthOfFieldFocalDistance` | 0.0 | Focus distance in cm (0=auto from focal point) |
| `DepthOfFieldFstop` | 4.0 | Aperture f-stop (lower = more blur). Range: 1.2-32.0 |
| `DepthOfFieldSensorWidth` | 24.576mm | Sensor width (Super35 default, affects CoC scale) |
| `DepthOfFieldFocalRegion` | 0.0 | Sharp focus width in cm (flat zone around focus) |
| `DepthOfFieldNearTransitionRegion` | 300.0 | Near blur transition width in cm |
| `DepthOfFieldFarTransitionRegion` | 500.0 | Far blur transition width in cm |
| `DepthOfFieldNearBlurSize` | 15.0 | Max near-field blur radius in % of screen width |
| `DepthOfFieldFarBlurSize` | 15.0 | Max far-field blur radius in % |
| `DepthOfFieldBladeCount` | 5 | Diaphragm blade count (5=pentagon, 6=hexagon, 8=octagon) |
| `DepthOfFieldMinFstop` | 1.2 | Minimum allowed f-stop |
| Method | Cinematic (Diaphragm) | Default method for high quality |

### UE5 Pipeline Position
After SSAO/SSR, before motion blur and TAA. DoF operates in HDR space.

## UzEngine Implementation Plan

### Shader Files
- `dof_coc_ps.hlsl` — CoC computation from depth + camera parameters
- `dof_downsample_ps.hlsl` — Downsample scene to half-res, separate near/far by CoC sign
- `dof_bokeh_cs.hlsl` — Gather-based bokeh blur (disk kernel with blade shape)
- `dof_recombine_ps.hlsl` — Composite near+focus+far layers

### Constant Buffer (register b2)
```hlsl
cbuffer DoFParams : register(b2) {
    float  focalDistance;        // Focus distance in world units
    float  fstop;                // Aperture (4.0 UE5 default)
    float  sensorWidth;          // 24.576mm (Super35)
    float  focalLength;          // Derived from FOV + sensor width
    float  focalRegion;          // Sharp zone width (0.0 default)
    float  nearTransition;       // Near blur ramp (300.0 UE5)
    float  farTransition;        // Far blur ramp (500.0 UE5)
    float  maxNearBlur;          // Max near blur radius pixels
    float  maxFarBlur;           // Max far blur radius pixels
    int    bladeCount;           // Diaphragm blades (5 UE5 default)
    int    gatherSamples;        // Samples per gather (22/43/71 by quality)
    float  cocScale;             // Pre-computed CoC multiplier
    float2 texelSize;
    float2 pad;
};
```

### Quality Levels (match UE5)
- **Low**: Gaussian blur (no bokeh shape), 22 samples, half-res → ~0.3ms
- **Medium**: Disk gather, 43 samples, half-res → ~0.6ms
- **High**: Diaphragm bokeh, 71 samples, half-res → ~1.0ms
- **Cinematic**: Full diaphragm, 128 samples, quarter+half-res → ~1.5ms

## Impact

- **Shaders**: 4 new HLSL files
- **Render graph**: CoC pass → downsample → bokeh blur → recombine
- **Textures**: CoC buffer (R16F), half-res near/far (RGBA16F each)
- **VRAM**: ~16MB at 1080p
- **Performance**: 0.3-1.5ms
- **Dependencies**: Depth buffer, scene color (HDR)

## Breaking Changes

None — DoF is additive.

## Acceptance Criteria

1. Objects at focal distance are sharp, foreground/background progressively blurred
2. Bokeh shape visible in specular highlights (pentagon with 5 blades)
3. No bleeding of in-focus content into near-field blur
4. f/1.2 produces extreme shallow DoF, f/16 produces minimal blur
5. Smooth transition between in-focus and out-of-focus regions
6. Demo screenshot: shallow DoF with foreground/background blur, bokeh on specular highlights
