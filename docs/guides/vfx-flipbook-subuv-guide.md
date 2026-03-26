<!-- doc-meta
title: VFX Flipbook/SubUV Configuration Guide
category: guide
engine: uzengine
theme: rendering
created: 2026-03-25
updated: 2026-03-25
status: active
author: docs-operator
-->

# VFX Flipbook/SubUV Configuration Guide

## Overview

Flipbook animation (also called SubUV animation) is a GPU-accelerated technique for animating particle sprite textures using sprite sheets. Instead of displaying a single sprite per particle, you pack multiple animation frames into a grid on a single texture and select the current frame based on particle age. This approach is memory-efficient and enables high-quality animation effects with minimal performance overhead.

This guide covers configuring flipbook animation in UzEngine's VFX system, including frame ordering, blend modes, and practical examples.

## Context

Flipbook animation is a core feature of the UzEngine VFX system, implemented in the `SpriteRenderModule`. The technique is described in detail in GPU Gems 3 Chapter 23 and is the industry standard for particle animation.

## Configuration

All flipbook settings are properties of the `SpriteRenderModule` and control how sprite sheets are sampled during particle rendering.

### Basic Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `flipbookRows` | uint32 | 1 | Number of rows in the sprite sheet atlas. Set to 1 if no flipbook animation is used. |
| `flipbookCols` | uint32 | 1 | Number of columns in the sprite sheet atlas. Set to 1 if no flipbook animation is used. |
| `flipbookTotalFrames` | uint32 | 1 | Total number of valid animation frames in the sheet. Can be less than `rows * cols` if the sheet has unused trailing cells. |
| `flipbookFPS` | float | 30.0 | Playback speed in frames per second. Controls how fast animation progresses over a particle's lifetime. Only used for animated blend modes (None, Linear). |
| `flipbookBlendMode` | enum | None | Frame selection and blending strategy (see [Blend Modes](#blend-modes)). |

### Detecting Flipbook Animation

Use the `HasFlipbook()` method to check if a sprite render module has flipbook animation enabled:

```cpp
if (sprite_module.HasFlipbook()) {
    // Module uses a sprite sheet with multiple frames
}
```

A sprite sheet is considered active if either `flipbookRows > 1` OR `flipbookCols > 1`. A 1x1 sheet (the default) means no animation.

## Blend Modes

The `FlipbookBlendMode` enum controls how frames are selected and blended during playback.

### None — Snap to Frame (Animated)

**Behavior**: Snaps to the nearest frame with no interpolation between frames.

**Use when**: You want discrete frame changes without visual smoothing. Good for pixelated effects or low frame-rate animations that intentionally "pop" between frames.

**Configuration**:
```cpp
sprite_module.flipbookBlendMode = uz::FlipbookBlendMode::None;
sprite_module.flipbookFPS = 30.0f; // Frames per second
```

**Visual result**: As a particle ages, it snaps between discrete frames without blending. At 30 FPS with a 4x4 sheet (16 frames), the animation completes in 16/30 ≈ 0.53 seconds.

### Linear — Interpolated Frames (Animated)

**Behavior**: Linearly interpolates (blends) between the current frame and the next frame, creating smooth transitions.

**Use when**: You want smooth, high-quality animation without visible frame boundaries. Standard for high-fidelity particle effects like explosions, fire, or magic.

**Configuration**:
```cpp
sprite_module.flipbookBlendMode = uz::FlipbookBlendMode::Linear;
sprite_module.flipbookFPS = 30.0f;
```

**Visual result**: As a particle ages, it smoothly transitions between frames. The blend factor is the fractional part of the continuous frame index, producing visually smooth motion even at low frame rates.

### Random — Deterministic Variety (Static)

**Behavior**: Each particle selects one deterministic random frame based on its particle index. The frame does not change over the particle's lifetime — it's static variety, not animation.

**Use when**: You need visual variety without animation. Typical use cases include rendering 8 different leaf sprites, 4 variant rock shapes, or 6 different cloud puffs, where each particle gets a random appearance but doesn't animate.

**Configuration**:
```cpp
sprite_module.flipbookBlendMode = uz::FlipbookBlendMode::Random;
// flipbookFPS is ignored in Random mode
```

**Implementation detail**: Frame selection uses Wang hash of the particle's index to produce a well-distributed pseudo-random frame number. The same particle index always maps to the same frame, ensuring deterministic, stable results.

## Frame Ordering

Frames are ordered **row-major**, **left-to-right**, **top-to-bottom** in the sprite sheet:

```
Frame Layout (4x4 sheet):
Frame 0   Frame 1   Frame 2   Frame 3
Frame 4   Frame 5   Frame 6   Frame 7
Frame 8   Frame 9   Frame 10  Frame 11
Frame 12  Frame 13  Frame 14  Frame 15
```

**In memory**: Frame index is computed as `frameIndex = row * cols + col`.

This ordering is consistent across all flipbook computation functions and matches the industry standard.

## HLSL Functions

The flipbook system exposes several HLSL functions for shader-side frame computation. These are located in `runtime/shaders/runtime/vfx/vfx_flipbook.hlsli`.

### FlipbookResult Structure

All frame computation functions return a `FlipbookResult` structure:

```hlsl
struct FlipbookResult {
    float2 uv0;         // Top-left UV of current frame
    float2 uv1;         // Top-left UV of next frame (for blending)
    float blendFactor;  // Lerp weight in [0, 1)
    float2 frameSize;   // Size of one frame in UV space: (1/cols, 1/rows)
};
```

### ComputeFlipbookFrame — Animated Frames

Computes frame UVs from particle age and playback rate.

```hlsl
FlipbookResult ComputeFlipbookFrame(
    float age,
    float lifetime,
    uint rows,
    uint cols,
    float fps,
    uint totalFrames)
```

**Parameters**:
- `age`: Current particle age in seconds
- `lifetime`: Total particle lifetime in seconds (for context; not directly used)
- `rows`: Number of rows in the sprite sheet
- `cols`: Number of columns in the sprite sheet
- `fps`: Playback speed in frames per second
- `totalFrames`: Total valid frames in the sheet

**Returns**: `FlipbookResult` with frame UVs and blend factor.

**Degenerate handling**: If `fps <= 0`, `totalFrames == 0`, or sheet dimensions are 0, returns frame 0 with no blending.

### ComputeFlipbookFrameRandom — Random Frame Selection

Selects a deterministic random frame per particle.

```hlsl
FlipbookResult ComputeFlipbookFrameRandom(
    uint particleIndex,
    uint rows,
    uint cols,
    uint totalFrames)
```

**Parameters**:
- `particleIndex`: Unique particle ID (hashed to select frame)
- `rows`: Number of rows in the sprite sheet
- `cols`: Number of columns in the sprite sheet
- `totalFrames`: Total valid frames in the sheet

**Returns**: `FlipbookResult` with a single selected frame (`uv0 == uv1`, `blendFactor == 0`).

### SampleFlipbookUV — UV Remapping

Transforms a normalized billboard UV into atlas sub-rectangle coordinates for a specific frame.

```hlsl
float2 SampleFlipbookUV(
    float2 baseUV,
    float2 frameUV,
    float2 frameSize)
```

**Parameters**:
- `baseUV`: Particle billboard UV in [0,1] x [0,1] range
- `frameUV`: Top-left corner of target frame (from `FlipbookResult.uv0` or `uv1`)
- `frameSize`: Size of one frame in UV space (from `FlipbookResult.frameSize`)

**Returns**: Atlas UV that samples the target frame sub-rectangle.

**Example**:
```hlsl
float2 uv0 = SampleFlipbookUV(baseUV, result.uv0, result.frameSize);
float2 uv1 = SampleFlipbookUV(baseUV, result.uv1, result.frameSize);
float4 color = lerp(
    texAtlas.Sample(sampler, uv0),
    texAtlas.Sample(sampler, uv1),
    result.blendFactor);
```

### Helper: FrameIndexToUV

Converts a linear frame index to the top-left UV coordinate in atlas space.

```hlsl
float2 FrameIndexToUV(
    uint frameIndex,
    uint cols,
    float invCols,      // Precomputed 1.0 / cols
    float invRows)      // Precomputed 1.0 / rows
```

### Helper: WangHash

Deterministic hash function for producing well-distributed pseudo-random numbers from a seed (used internally by `ComputeFlipbookFrameRandom`).

```hlsl
uint WangHash(uint seed)
```

## Usage Examples

### Example 1: Fire Explosion (4x4 Animated)

A classic explosion effect with 16 animation frames.

```cpp
SpriteRenderModule fire_sprite;
fire_sprite.flipbookRows = 4;
fire_sprite.flipbookCols = 4;
fire_sprite.flipbookTotalFrames = 16;  // Full sheet, no gaps
fire_sprite.flipbookFPS = 30.0f;       // Complete animation in ~0.53 seconds
fire_sprite.flipbookBlendMode = uz::FlipbookBlendMode::Linear;
fire_sprite.spriteWidth = 2.0f;
fire_sprite.spriteHeight = 2.0f;
```

**Sprite sheet layout**: 512x512 texture with 16 frames arranged 4 rows × 4 columns. Each frame is 128x128 pixels.

### Example 2: Explosion Burst (8x8 Partial)

An 8x8 sprite sheet where only the first 60 frames are valid animation frames (4 frames are empty).

```cpp
SpriteRenderModule burst_sprite;
burst_sprite.flipbookRows = 8;
burst_sprite.flipbookCols = 8;
burst_sprite.flipbookTotalFrames = 60;  // Only 60 of 64 cells are valid
burst_sprite.flipbookFPS = 24.0f;       // Slower animation
burst_sprite.flipbookBlendMode = uz::FlipbookBlendMode::Linear;
burst_sprite.spriteWidth = 1.5f;
burst_sprite.spriteHeight = 1.5f;
```

**Sprite sheet layout**: 1024x1024 texture with 60 valid animation frames. The last 4 cells (positions 60-63) are empty and will not be sampled.

### Example 3: Leaf Variety (1x8 Random)

Eight different leaf sprites arranged in a single row. Each particle picks one random leaf and maintains that appearance throughout its lifetime.

```cpp
SpriteRenderModule leaf_sprite;
leaf_sprite.flipbookRows = 1;
leaf_sprite.flipbookCols = 8;
leaf_sprite.flipbookTotalFrames = 8;
// flipbookFPS is ignored
leaf_sprite.flipbookBlendMode = uz::FlipbookBlendMode::Random;
leaf_sprite.spriteWidth = 0.5f;
leaf_sprite.spriteHeight = 0.5f;
```

**Sprite sheet layout**: 512x64 texture with 8 leaf variants side-by-side. Each particle gets a deterministic random leaf based on its index.

### Example 4: Simple Smoke (No Flipbook)

A single static sprite with no animation.

```cpp
SpriteRenderModule smoke_sprite;
// Leave flipbookRows and flipbookCols at default 1
smoke_sprite.spriteWidth = 3.0f;
smoke_sprite.spriteHeight = 3.0f;
```

**Sprite sheet layout**: Standard texture, no sprite sheet packing required. The entire texture is sampled as a single sprite.

## Edge Cases and Degenerate Handling

The flipbook system is designed to be robust against invalid inputs. All functions handle degenerate cases gracefully:

### fps == 0 or fps < 0

Returns frame 0 with no blending. This prevents division by zero and provides a safe fallback.

### totalFrames == 0

Returns frame 0 with no blending. If the frame count is unspecified or zero, no animation occurs.

### rows == 0 or cols == 0

Returns frame 0 with zero frameSize to avoid division by zero. The result is unusable for texture sampling, but the function doesn't crash.

### lifetime == 0

Doesn't cause errors in `ComputeFlipbookFrame` — the function only uses age and fps to compute the continuous frame index. A particle with zero lifetime still animates normally; the caller is responsible for lifetime validation.

### age > lifetime

The continuous frame index is computed from age and fps without lifetime checks, so particles can animate beyond their lifetime if the caller doesn't cull them. This is intentional — lifetime management is the caller's responsibility.

## Performance Considerations

- **GPU cost**: Flipbook computation is negligible — a single `ComputeFlipbookFrame` call costs roughly 5-10 GPU cycles.
- **Memory**: All flipbook parameters are packed into the `SpriteRenderModule` structure. Each parameter uses 4 or 8 bytes; the total overhead is minimal.
- **Texture bandwidth**: Sampling flipbook textures has the same cost as any standard texture sample. Linear filtering may slightly increase bandwidth compared to point sampling due to four-tap filtering per frame.
- **Interpolation cost**: Linear blend mode requires two texture samples + one lerp per pixel. This is standard and widely used in production.

## Key Takeaways

- Flipbook animation uses sprite sheet textures to pack multiple animation frames efficiently.
- **Three blend modes**: `None` (snap, discrete), `Linear` (smooth interpolation), `Random` (per-particle variety).
- **Frame ordering**: Row-major, left-to-right, top-to-bottom. Frame index = row * cols + col.
- **Configuration**: Set `flipbookRows`, `flipbookCols`, `flipbookTotalFrames`, `flipbookFPS`, and `flipbookBlendMode` on the `SpriteRenderModule`.
- **HLSL support**: Use `ComputeFlipbookFrame` for animated modes, `ComputeFlipbookFrameRandom` for variety, and `SampleFlipbookUV` to remap texture coordinates.
- **Partial sheets**: Use `flipbookTotalFrames < rows * cols` when the sprite sheet has trailing empty cells.

## References

- @ref Nguyen 2007 "GPU Gems 3, Chapter 23: GPU Particle Systems" — detailed flipbook animation techniques
- @ref McGuire 2006 "Particle Systems" — sprite sheet animation fundamentals
- `runtime/engine/vfx/modules/sprite_render_module.h` — Configuration parameters
- `runtime/shaders/runtime/vfx/vfx_flipbook.hlsli` — HLSL implementation

## Changelog

| Date | Change |
|------|--------|
| 2026-03-25 | Initial creation with comprehensive configuration guide, blend modes, HLSL functions, and examples. |
