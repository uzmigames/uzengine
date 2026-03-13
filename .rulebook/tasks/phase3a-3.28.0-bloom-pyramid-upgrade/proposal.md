# Bloom Pyramid Upgrade (UE5 Parity)

## Priority: P0 — Huge visual quality improvement for minimal effort

## Summary

Upgrade bloom from single-mip 5-tap Gaussian to UE5's 6-stage progressive downsample/upsample pyramid. Current bloom is narrow and harsh — UE5's pyramid produces wide, soft, natural-looking glow by accumulating blur across 6 resolution levels (1/2 → 1/64).

## Current UzEngine Bloom vs UE5

| Aspect | UzEngine (Current) | UE5 (Target) |
|--------|-------------------|---------------|
| **Stages** | 1 mip (half-res only) | **6 stages** (1/2, 1/4, 1/8, 1/16, 1/32, 1/64) |
| **Filter** | 5-tap Gaussian (σ≈1.0) | **13-tap Gaussian** per stage + Karis average on first downsample |
| **Blur radius** | ~5 pixels | **~1000+ pixels** (accumulated across mips) |
| **Visual** | Tight halo around bright spots | **Wide, soft, natural glow** with long falloff |
| **Threshold** | Hard luminance cutoff | **Soft knee**: `max(0, luma - threshold + knee) / (2*knee)` |
| **Intensity** | Single multiplier | **Per-stage weights** (artistic control per blur size) |

## UE5 Reference (Bloom.usf / PostProcessBloom.cpp)

### Algorithm: Progressive Downsample + Upsample
1. **Downsample chain** (top to bottom):
   - Stage 0: Scene (full-res) → 1/2 res (threshold + Karis average to prevent fireflies)
   - Stage 1: 1/2 → 1/4
   - Stage 2: 1/4 → 1/8
   - Stage 3: 1/8 → 1/16
   - Stage 4: 1/16 → 1/32
   - Stage 5: 1/32 → 1/64

2. **Upsample chain** (bottom to top):
   - Start at 1/64 (widest blur)
   - Each stage: bilinear upsample + add weighted previous stage
   - Result: sum of Gaussians at increasing radii

3. **Composite**: Final upsampled result added to scene × BloomIntensity

### UE5 Default Parameters
| Parameter | Value | Description |
|-----------|-------|-------------|
| `BloomIntensity` | 0.675 | Global bloom multiplier |
| `BloomThreshold` | -1.0 | Luminance threshold (-1 = all pixels bloom, UE5 default) |
| `BloomSizeScale` | 4.0 | Blur kernel scale |
| `Bloom1Size` | 0.3 | Stage 1 blur kernel radius |
| `Bloom2Size` | 0.3 | Stage 2 |
| `Bloom3Size` | 0.3 | Stage 3 |
| `Bloom4Size` | 0.3 | Stage 4 |
| `Bloom5Size` | 0.3 | Stage 5 |
| `Bloom6Size` | 0.3 | Stage 6 |
| `Bloom1Tint` | (0.3465, 0.3465, 0.3465) | Stage 1 color weight |
| `Bloom2Tint` | (0.138, 0.138, 0.138) | Stage 2 |
| `Bloom3Tint` | (0.1176, 0.1176, 0.1176) | Stage 3 |
| `Bloom4Tint` | (0.066, 0.066, 0.066) | Stage 4 |
| `Bloom5Tint` | (0.066, 0.066, 0.066) | Stage 5 |
| `Bloom6Tint` | (0.061, 0.061, 0.061) | Stage 6 |

### Karis Average (first downsample only)
Prevents specular fireflies from dominating bloom:
```hlsl
float w = 1.0 / (1.0 + Luminance(color));
downsampled = (c0*w0 + c1*w1 + c2*w2 + c3*w3) / (w0 + w1 + w2 + w3);
```

### Soft Threshold Knee
Instead of hard `luma > threshold ? color : 0`:
```hlsl
float knee = threshold * 0.5;
float soft = clamp(luma - threshold + knee, 0, 2*knee);
float contribution = soft * soft / (4*knee + 1e-5);
color *= max(contribution, step(threshold, luma));
```

## Impact

- **Shaders**: Modify bloom_downsample_ps (add Karis + soft knee), bloom_blur → replace with upsample shader, add bloom_upsample_ps
- **Render graph**: 6 downsample passes + 5-6 upsample passes (replacing current 3 passes)
- **Textures**: 6 mip levels instead of 1 (~1.33× scene resolution total = ~5.3MB at 1080p RGBA16F)
- **Performance**: ~0.3ms total (vs ~0.15ms current) — still cheap
- **Visual**: Dramatically improved — wide, soft, cinematic glow

## Acceptance Criteria

1. Bloom halo extends much wider than current (visible glow radius 100+ pixels)
2. Bright specular highlights don't produce firefly artifacts (Karis average works)
3. Soft threshold produces smooth transition (no hard on/off at luminance boundary)
4. Per-stage tints match UE5 defaults — inner glow warm, outer glow cool (if tinted)
5. Demo: bright light source showing wide, soft bloom halo matching UE5 reference
