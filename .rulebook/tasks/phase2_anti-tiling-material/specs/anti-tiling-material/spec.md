# Anti-Tiling Material System — Spec Delta

## ADDED Requirements

### Requirement: World-Space Macro Noise Blending
The anti-tiling system MUST generate a macro noise mask in world space (XY or XZ depending on surface orientation) to blend between two texture samples at different scales. World-space coordinates SHALL be used to avoid UV-seam artifacts on large meshes with discontinuous UVs.

#### Scenario: Flat terrain with default parameters
Given a 20x20m planar surface with a single tiled PBR material (albedo + normal + roughness)
When anti-tiling is enabled with default parameters
Then the visible repetition pattern SHALL be broken by smooth blending between two sample scales
And no sharp seam or discontinuity SHALL be visible at UV boundaries

#### Scenario: Non-planar mesh (curved wall)
Given a curved mesh with standard UV mapping
When anti-tiling is enabled using world-space macro noise
Then the macro variation SHALL be continuous across UV seams
And the blending pattern SHALL not shift when the camera moves (world-space stability)

### Requirement: Dual-Sample Texture Blending
The system MUST sample the base PBR texture (albedo, normal, roughness) at two different scales. The second sample SHALL use a configurable scale offset and optional rotation to maximize decorrelation from the primary sample.

#### Scenario: Variation with rotation
Given baseTiling = 1.0 and variationTiling = 0.37 and rotationAngle = 45 degrees
When the shader samples both layers
Then the second sample SHALL be rotated 45 degrees relative to the first
And the blended result SHALL show no obvious tiling pattern at medium viewing distance

#### Scenario: Variation strength at zero
Given variationStrength = 0.0
When the shader evaluates anti-tiling
Then the output SHALL be identical to a single standard texture sample (no blending cost visible in output)

### Requirement: Dark Spots / Dirt Breakup Layer
The system MUST generate a second, lower-frequency procedural noise that modulates albedo brightness and optionally roughness. This creates large-scale color variation (darker patches, weathering) independent of the tiling pattern.

#### Scenario: Dark spots affect albedo
Given darkSpotsScale = 50.0 and darkSpotsStrength = 0.15
When the shader evaluates the dark spots layer
Then albedo SHALL be multiplied by a smooth factor in the range [1.0 - darkSpotsStrength, 1.0]
And the spatial frequency of the variation SHALL correspond to darkSpotsScale in world units

#### Scenario: Dark spots disabled
Given darkSpotsStrength = 0.0
When the shader evaluates the dark spots layer
Then the albedo and roughness SHALL be unmodified by this layer

### Requirement: Microdetail Normal Overlay
The system MUST support an independent normal/bump detail layer that is NOT affected by the macro blending. This preserves surface detail (pores, grain, scratches) that would otherwise be smoothed out by the dual-sample blend.

#### Scenario: Bump detail preserved after blending
Given a material with bumpStrength = 0.5 and a normal map
When the shader blends the two macro samples
Then the microdetail normal SHALL be applied on top of the blended normal
And the detail frequency SHALL be independent of baseTiling and variationTiling

### Requirement: Exposed Parameters with Sane Defaults
The system MUST expose at minimum 9 parameters, each with a documented default value. All parameters MUST be settable per-material.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| baseTiling | float | 1.0 | Primary UV scale multiplier |
| variationTiling | float | 0.37 | Secondary sample UV scale (decorrelated from base) |
| variationStrength | float | 0.5 | Blend strength between primary and secondary samples [0, 1] |
| macroNoiseScale | float | 100.0 | World-space scale of the blending noise (in cm) |
| macroNoiseContrast | float | 0.5 | Contrast of the noise mask [0, 1] |
| darkSpotsScale | float | 200.0 | World-space scale of the dirt/darkening noise (in cm) |
| darkSpotsStrength | float | 0.1 | Maximum darkening multiplier [0, 1] |
| bumpStrength | float | 0.3 | Microdetail normal overlay intensity [0, 1] |
| rotationAngle | float | 0.0 | Rotation of the variation sample in degrees |

#### Scenario: All parameters at defaults
Given a material with all anti-tiling parameters at their default values
When the shader renders a large tiled surface
Then the result SHALL show reduced repetition compared to standard single-sample tiling
And the performance overhead SHALL be less than 0.3ms at 1080p on a mid-range GPU

### Requirement: Performance Budget
The anti-tiling shader path MUST NOT exceed 3 texture lookups per pixel for the base material channels. Noise MUST be procedural (hash-based), not texture-based, to avoid additional SRV slots. Total additional GPU cost SHALL be under 0.3ms at 1080p for a full-screen terrain surface.

#### Scenario: Texture lookup count
Given the ANTI_TILING shader variant is active
When rendering a pixel with albedo + normal + roughness
Then the total texture sample count SHALL be at most 6 (2 samples x 3 channels: albedo, normal, roughness) plus procedural noise (no texture lookups)
And the noise functions SHALL use ALU-only computation (hash, smoothstep, lerp)

### Requirement: Shader Variant Integration
The anti-tiling path MUST be gated behind an `ANTI_TILING` preprocessor define managed by the shader variant system. Materials without anti-tiling enabled SHALL have zero overhead (no extra ALU, no extra texture samples).

#### Scenario: Variant disabled
Given a material without the ANTI_TILING flag
When the GBuffer pass renders this material
Then the shader SHALL follow the standard single-sample path
And no anti-tiling code SHALL execute
