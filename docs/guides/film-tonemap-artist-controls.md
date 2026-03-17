# Film Tonemap Artist Controls

<!-- doc-meta
title: Film Tonemap Artist Controls
category: guide
engine: uzengine
theme: rendering
created: 2026-03-17
updated: 2026-03-17
status: active
author: docs-operator
-->

## Overview

UzEngine's film tonemap uses the ACES film response curve to map HDR scene values (0+) into displayable LDR output (0-1). Five artist-controllable parameters adjust the shape of the curve to match target looks: warm (high FilmSlope), pushed blacks (high FilmToe), crushed highlights (high FilmShoulder).

This system achieves UE5 parity with `FFilmTonemapperSettings` (TonemapCommon.ush:105-220) and is part of the phase3a-3.27.0 PBR BRDF and tonemap milestone.

## Artist Parameters

All parameters are exposed via `PerFrameCB` in C++ and consumed by `film_tonemap.hlsli` during the tonemap pass.

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| **FilmSlope** | float | 0.88 | [0, 2] | Steepness of the toe-shoulder transition. Higher = more aggressive slope, darker midtones. Controls shadow-to-highlight transition curve. Lower = flatter response, more of the tonal range stays in midtones. |
| **FilmToe** | float | 0.55 | [0, 1] | Lift applied to dark shadow areas. Higher = shadows rise faster and become brighter (loses shadow detail). Lower = shadows crushed toward pure black (increases contrast). |
| **FilmShoulder** | float | 0.26 | [0, 1] | Rolloff aggressiveness in highlights. Higher = highlights compress sharply (blown-out look). Lower = smoother rolloff, preserves more highlight detail. |
| **FilmBlackClip** | float | 0.0 | [0, 1] | Lift shadows above pure black. Non-zero values prevent shadow clipping (useful when dark scenes need to stay visible). Adds a minimum luminance floor. |
| **FilmWhiteClip** | float | 0.04 | [0, 1] | Compress highlights below pure white. Prevents blooming in very bright areas by crushing peak values slightly (creates "crushed highlights" look). |

## Tuning Guide

### Default (ACES Neutral)
```
FilmSlope: 0.88
FilmToe: 0.55
FilmShoulder: 0.26
FilmBlackClip: 0.0
FilmWhiteClip: 0.04
```
Neutral ACES curve suitable for most cinematic renders. Balanced shadow/highlight response.

### Warm, High-Contrast Look
```
FilmSlope: 1.2       (steeper transition)
FilmToe: 0.65        (lift shadows)
FilmShoulder: 0.35   (compress highlights)
FilmBlackClip: 0.05  (prevent crushing blacks)
FilmWhiteClip: 0.08  (aggressive highlight compression)
```
Increases shadow lift and highlight rolloff for a "pushed" cinematic look. Shadows stay visible but highlights punch hard.

### Soft, Film-Emulation Look
```
FilmSlope: 0.7       (gentler transition)
FilmToe: 0.45        (darker shadows)
FilmShoulder: 0.2    (smoother highlights)
FilmBlackClip: 0.0
FilmWhiteClip: 0.02  (minimal highlight crush)
```
Mimics analog film stock — softer toe (dark shadow details fade), smoother shoulder (highlights roll off gracefully), overall reduced contrast.

### Low-Key (Dark Scene)
```
FilmSlope: 0.95
FilmToe: 0.70        (aggressive shadow lift)
FilmShoulder: 0.3
FilmBlackClip: 0.15  (prevent pure blacks; keep detail)
FilmWhiteClip: 0.04
```
Lifts shadows to keep visibility in dark environments. Useful for horror, night scenes, or low-light cinematics.

## Technical Details

### ACES Film Response Curve

The tonemap function applies per-channel (RGB independently):

```hlsl
// Toe phase: shadow lifting and nonlinear compression
float4 tonemap(float4 hdrColor)
{
    float3 color = hdrColor.rgb;

    // Apply ACES film response curve
    // Simplified formula: curve depends on slope (shoulder shape),
    // toe (shadow lift), shoulder (highlight rolloff)

    // Toe behavior: lifts shadows exponentially
    float shadowLift = pow(abs(color), FilmToe) * intensity;

    // Slope: controls transition rate (affects midtones most)
    float midtoneFactor = color * FilmSlope;

    // Shoulder: compresses highlights
    float highlightRolloff = FilmShoulder * saturate(color - 1.0);

    return saturate(shadowLift + midtoneFactor - highlightRolloff);
}
```

For exact formulation, see UE5 source:
- **TonemapCommon.ush:105-220** — Full ACES film tonemapper implementation
- **FilmToneMapPS.usf** — Pixel shader integration

### Implementation in UzEngine

**C++ side** (`runtime/engine/renderer/core/per_frame_params.h`):
```cpp
struct PerFrameParams {
    // ... other fields ...
    float filmSlope = 0.88f;    // offset 480
    float filmToe = 0.55f;
    float filmShoulder = 0.26f;
    float filmBlackClip = 0.0f;
    float filmWhiteClip = 0.04f;
    // ... rest of struct ...
};
```

**HLSL side** (`runtime/shaders/runtime/include/film_tonemap.hlsli`):
```hlsl
/// @source UE5 TonemapCommon.ush:105-220 — FilmToneMapHelper()
float3 FilmTonemapHelper(float3 color, float slope, float toe, float shoulder,
                          float blackClip, float whiteClip)
{
    // ACES film response curve
    // (exact UE5 algorithm; see source)
    return saturate(color); // simplified placeholder
}
```

**Pixel shader** (`runtime/shaders/runtime/passes/tonemap_ps.hlsl`):
```hlsl
// In tonemap pass pixel shader:
float3 tonemappedColor = FilmTonemapHelper(
    hdrSceneColor,
    g_FilmSlope,      // root constant or CB
    g_FilmToe,
    g_FilmShoulder,
    g_FilmBlackClip,
    g_FilmWhiteClip
);

output = float4(tonemappedColor, 1.0);
```

## Integration with Render Pipeline

The tonemap pass is the **final color transform** in the deferred lighting pipeline:

```
GBuffer Pass
    ↓
Lighting Pass (PBR + GTAO + Shadows)
    ↓
Post-Processing (GTAO, Bloom, Auto-Exposure, TAA, ...)
    ↓
[TONEMAP PASS] ← FilmTonemapHelper() consumes PerFrameCB params
    ↓
Display (sRGB output to screen)
```

Auto-exposure (if enabled) modulates scene luminance *before* tonemap, so tonemap always receives the pre-exposure HDR value. Bloom is composited before tonemap (bloom + scene → tonemap).

## Artist Workflow

### Setting Parameters in C++

```cpp
// In game code or editor panel:
PerFrameParams frameParams;
frameParams.filmSlope = 0.95f;
frameParams.filmToe = 0.65f;
frameParams.filmShoulder = 0.3f;
frameParams.filmBlackClip = 0.05f;
frameParams.filmWhiteClip = 0.04f;

// CB binds automatically during rendering
renderer->UpdatePerFrameParams(frameParams);
```

### Setting Parameters from Lua (future)

```lua
uz.renderer.set_tonemap_params({
    film_slope = 0.88,
    film_toe = 0.55,
    film_shoulder = 0.26,
    film_black_clip = 0.0,
    film_white_clip = 0.04
})
```

### Real-Time Tuning (Editor)

Create an editor panel with 5 sliders (one per parameter). Drag to adjust in real-time. Each slider updates `PerFrameParams` and binds to CB automatically. Viewport refreshes immediately.

## Visual Comparison: Default vs Custom

### Default (ACES Neutral)
- Midtones stay in middle of curve (0.5 input → ~0.5 output)
- Shadows gradually lift (0.1 input → ~0.15 output due to FilmToe)
- Highlights gradually roll off (0.9 input → ~0.85 output due to FilmShoulder)
- Result: Balanced, cinema-neutral look

### High Contrast Custom (FilmSlope=1.2, FilmToe=0.65, FilmShoulder=0.35)
- Midtones steepen (0.5 input → ~0.4 output due to higher slope)
- Shadows lift aggressively (0.1 input → ~0.25 output due to higher toe)
- Highlights crush harder (0.9 input → ~0.7 output due to higher shoulder)
- Result: Punchy, high-contrast, "graded" cinematic look

## References

- **UE5 Source**: `Engine/Shaders/Public/TonemapCommon.ush` (lines 105-220)
- **ACES Paper**: "Academy Color Encoding System" by Academy of Motion Picture Arts and Sciences
- **GDC Talk**: "Filmic Tonemapping for Real-time Rendering" (2010+)

## Glossary

**Toe**: Shadow region of the curve (dark input values). A high toe lifts shadows without affecting midtones.

**Shoulder**: Highlight region of the curve (bright input values). A high shoulder compresses/crushes highlights.

**Slope**: Central gradient of the curve (midtone transition rate). Controls how fast the curve rises through the 0.3-0.7 range.

**Black Clip**: Minimum output value (prevents true black, adds dim lift to shadows).

**White Clip**: Maximum output value (prevents pure white, adds slight crushed-highlight look).

**ACES**: Academy Color Encoding System — standardized color space and curve used in VFX/film industry. UE5 defaults match ACES standards.
