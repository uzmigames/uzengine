# Stochastic Screen-Space Reflections — Spec Delta

## ADDED Requirements

### Requirement: Temporal IGN Jitter
The SSR trace pass MUST use Interleaved Gradient Noise with a per-frame temporal rotation
offset of `float2(47, 17) * 0.695 * (frameIndex % 8)`, producing 8 distinct jitter phases
that enable temporal accumulation to converge.

#### Scenario: Static Camera Convergence
Given a static camera viewing a reflective surface
When 8 frames have been rendered with temporal IGN jitter
Then the temporal accumulation output SHALL show no visible per-pixel shimmer
And single-frame trace noise SHALL be invisible in the temporal result

#### Scenario: Frame Index Wrapping
Given the frame counter exceeds 2^32
When the IGN temporal offset is computed
Then the offset SHALL wrap correctly via `frameIndex % 8`
And no discontinuity SHALL appear at the wrap boundary

### Requirement: Hi-Z Pyramid Binding
The SSR trace pass MUST read from the Hi-Z depth mip chain (`m_hizMipLevels[]`) instead
of the raw full-resolution depth buffer (`m_gbuffer.depthSRV`).

#### Scenario: Hi-Z Mip Available
Given the Hi-Z pyramid has been built with N mip levels
When the SSR trace pass is dispatched
Then `hizSrvIdx` SHALL be set to `m_hizMipLevels[0].srvIndex`
And the shader SHALL sample the Hi-Z pyramid for depth comparison

#### Scenario: Hi-Z Mip Unavailable (fallback)
Given the Hi-Z pyramid has 0 mip levels (not built)
When the SSR trace pass binds resources
Then `hizSrvIdx` SHALL fall back to `m_gbuffer.depthSRV`
And the trace SHALL still produce valid (though lower quality) output

### Requirement: Karis Inverse Tonemap in Spatial Filter
The SSR resolve pass MUST apply Karis inverse tonemap `color *= rcp(1 + Luminance(color))`
to each sample before bilateral accumulation, and apply the inverse
`result *= rcp(1 - Luminance(result))` after normalization.

#### Scenario: HDR Firefly Suppression
Given a scene with a bright specular highlight (luminance > 10.0) reflected in SSR
When the resolve pass spatially filters the trace output
Then the bright pixel SHALL NOT dominate the kernel
And the filtered output SHALL have luminance within 2x of the kernel median

### Requirement: YCoCg Variance-Based Temporal Clipping
The SSR temporal accumulation pass MUST compute the 3x3 neighborhood mean and standard
deviation in YCoCg color space and clip history to `mean +/- 1.25 * sigma` instead of
using min/max AABB in RGB space.

#### Scenario: Saturated Color Preservation
Given a scene with saturated red reflections (R=1.0, G=0.1, B=0.1) in SSR
When the temporal pass clips history against the current neighborhood
Then the history SHALL NOT be over-clamped to desaturated values
And the YCoCg variance clip SHALL preserve chrominance better than RGB AABB

#### Scenario: Disocclusion Rejection
Given a camera movement that reveals previously occluded geometry
When the temporal pass detects the new content via variance deviation > 1.25 sigma
Then the history SHALL be rejected (blend weight -> 1.0 for current frame)
And the new content SHALL appear within 1-2 frames without ghosting

### Requirement: Adaptive Temporal Blend Weight
The SSR temporal pass MUST use an adaptive blend weight `alpha = 1.0 / min(targetSamples,
accumulatedCount + 1)` where `targetSamples = lerp(1, 64, roughness * roughness)` instead
of a fixed `TEMPORAL_BLEND_ALPHA = 0.05`.

#### Scenario: Mirror Surface Responsiveness
Given a surface with roughness = 0.04 (near-mirror)
When the SSR temporal pass blends history with current
Then `targetSamples` SHALL be approximately 1
And `alpha` SHALL be approximately 1.0 (100% current frame, instant response)

#### Scenario: Rough Surface Accumulation
Given a surface with roughness = 0.5
When the SSR temporal pass has accumulated 30 frames of history
Then `targetSamples` SHALL be approximately 16
And `alpha` SHALL be approximately `1/16` (gradual blend, smooth result)

### Requirement: CVar-Driven Resolve Parameters
The SSR resolve pass MUST read `intensity` and `roughnessMaskScale` from CVars
(`r.SSR.Intensity` and `r.SSR.MaxRoughness`) instead of hardcoded values.

#### Scenario: User Adjusts SSR Intensity
Given the user sets `r.SSR.Intensity = 0.5` via console
When the SSR resolve pass executes
Then the resolve `intensity` root constant SHALL be 0.5
And SSR contribution to the final image SHALL be halved

### Requirement: Binary Refinement Safety
The SSR trace pass binary refinement loop MUST check screen bounds `[0, 1]` for each
midpoint sample and MUST skip refinement when `abs(ssDir.z) < 1e-5` (grazing angle).

#### Scenario: Ray Exits Screen During Refinement
Given a ray whose binary refinement midpoint falls outside UV [0, 1]
When the refinement iteration computes ssMid
Then the loop SHALL break early
And hitUVZ SHALL retain the last valid position

#### Scenario: Grazing Angle Ray
Given a ray nearly parallel to the depth buffer (ssDir.z < 1e-5)
When binary refinement is entered
Then the refinement loop SHALL be skipped entirely
And the linear march hit position SHALL be used as-is

### Requirement: GGX VNDF Importance Sampling (Phase 3)
The SSR trace pass MUST implement Heitz 2018 Visible Normal Distribution Function sampling
for surfaces with roughness > 0.04, using STBN blue noise (Wolfe 2022) as the random seed.

#### Scenario: Rough Surface Reflection Lobe
Given a surface with roughness = 0.3
When the trace pass generates the reflection direction
Then the direction SHALL be sampled from the GGX VNDF lobe centered around the mirror direction
And the STBN seed SHALL vary per (pixel.xy % 128, frameIndex % 64)

#### Scenario: Mirror Surface Bypass
Given a surface with roughness <= 0.04
When the trace pass generates the reflection direction
Then the direction SHALL be the deterministic mirror reflection `reflect(-V, N)`
And no importance sampling SHALL be performed

### Requirement: STBN Blue Noise Texture (Phase 3)
The renderer MUST load a 128x128x64 spatiotemporal blue noise volume texture (Wolfe 2022,
NVIDIA open-source, MIT license) and bind it to the SSR trace pass as a 3D SRV.

#### Scenario: STBN Texture Load
Given the renderer initializes
When the STBN texture asset is found at `runtime/content/textures/stbn_128x128x64.bin`
Then a 128x128x64 R8G8 3D texture SHALL be created
And the SRV SHALL be stored in `m_stbnSrv`

#### Scenario: STBN Texture Missing (graceful fallback)
Given the STBN texture asset is not found
When the renderer initializes
Then SSR SHALL fall back to IGN temporal jitter (Phase 1 behavior)
And a warning SHALL be logged: "STBN texture not found, SSR using IGN fallback"

### Requirement: Virtual Hit Reprojection (Phase 3)
The SSR temporal pass MUST reproject history using the world-space hit point (reconstructed
from hit distance + ray direction) instead of the surface pixel's motion vector.

#### Scenario: Camera Parallax During Orbit
Given a camera orbiting around a reflective sphere
When the temporal pass reprojects SSR history
Then the reprojection SHALL use the hit point's world-space position projected through prev VP
And reflections of static geometry SHALL remain stable (no swimming)

### Requirement: Tile Classification (Phase 3)
A tile classification compute pass MUST classify 8x8 pixel tiles by maximum roughness from
the GBuffer normal texture. Tiles where max roughness exceeds `r.SSR.MaxRoughness` SHALL
be excluded from the ray dispatch via ExecuteIndirect.

#### Scenario: Fully Rough Tile Skip
Given an 8x8 tile where all pixels have roughness > MaxRoughness (0.35 default)
When tile classification runs
Then the tile SHALL NOT be included in the indirect dispatch argument buffer
And no rays SHALL be traced for that tile

#### Scenario: Mixed Roughness Tile
Given an 8x8 tile where some pixels have roughness < MaxRoughness
When tile classification runs
Then the tile SHALL be included in the indirect dispatch
And the trace shader SHALL still per-pixel skip roughness > MaxRoughness via RoughnessFade

### Requirement: Cascade Reflection Architecture (Phase 4)
The reflection system MUST output `(color, confidence, hitDistance, roughness)` per pixel
from the SSR pass, enabling a cascade compositor to blend SSR (Layer 1) with reflection
captures (Layer 2) and IBL (Layer 3) by confidence weight.

#### Scenario: SSR Miss Falls Through to IBL
Given a pixel where SSR trace found no hit (confidence = 0.0)
When the cascade compositor runs
Then the pixel SHALL receive 100% IBL specular
And no SSR contribution SHALL be applied

#### Scenario: Partial SSR Confidence Blends with IBL
Given a pixel where SSR confidence = 0.6 (near screen edge)
When the cascade compositor runs
Then the pixel SHALL receive 60% SSR + 40% IBL (weighted by confidence)

### Requirement: Performance Budget
The complete SSR pipeline (trace + resolve + temporal) MUST execute within 3.0ms GPU time
at 1920x1080 for Phase 1-2 and within 4.0ms for Phase 3 (with tile classification and VNDF).

#### Scenario: Phase 1-2 Performance
Given a 1920x1080 render target
When SSR Phase 1-2 pipeline executes (trace + resolve + temporal)
Then total GPU time SHALL be <= 3.0ms as measured by --gpu-profile

#### Scenario: Phase 3 Performance
Given a 1920x1080 render target with VNDF + tile classification
When SSR Phase 3 pipeline executes (classify + trace + resolve + temporal)
Then total GPU time SHALL be <= 4.0ms as measured by --gpu-profile
