# Advanced Material System — Spec Delta

## ADDED Requirements

### Requirement: Shading Model Enum

The material system MUST define an `EShadingModel` enum with at least 10 values: Unlit(0), DefaultLit(1), Subsurface(2), SubsurfaceProfile(3), ClearCoat(4), TwoSidedFoliage(5), Hair(6), Cloth(7), Eye(8), ThinTranslucent(9). The enum MUST be stored as a `uint8_t` and be serializable to/from `.uzmat` JSON.

#### Scenario: Material loads shading model from file
Given a `.uzmat` file with `"shadingModel": "Subsurface"`
When the material asset loader parses the file
Then `material.shadingModel` SHALL equal `EShadingModel::Subsurface`
And the value SHALL persist through save/load round-trip

#### Scenario: Default shading model is DefaultLit
Given a new `Material` constructed with default parameters
When no `shadingModel` field is set
Then `material.shadingModel` SHALL equal `EShadingModel::DefaultLit`

### Requirement: Material Domain Enum

The material system MUST define an `EMaterialDomain` enum with at least 5 values: Surface(0), Decal(1), PostProcess(2), LightFunction(3), UI(4). Each material MUST have a domain that determines which render pass processes it.

#### Scenario: Surface domain routes to GBuffer pass
Given a material with `materialDomain == EMaterialDomain::Surface` and `blendMode == EBlendMode::Opaque`
When the scene renderer builds draw commands
Then the draw command SHALL be added to the GBuffer draw list
And the draw command SHALL NOT appear in the Decal or PostProcess pass lists

#### Scenario: Decal domain routes to Decal pass
Given a material with `materialDomain == EMaterialDomain::Decal`
When the scene renderer builds draw commands
Then the draw command SHALL be added to the Decal pass draw list
And the draw command SHALL NOT appear in the GBuffer draw list

### Requirement: Blend Mode Enum

The material system MUST define an `EBlendMode` enum with at least 5 values: Opaque(0), Masked(1), Translucent(2), Additive(3), Modulate(4). The blend mode MUST replace the `materialFlags` bits[1:0] encoding.

#### Scenario: Masked blend mode performs alpha test
Given a draw command with `blendMode == EBlendMode::Masked` and `alphaClipThreshold == 0.5`
When the GBuffer pixel shader evaluates a fragment with opacity 0.3
Then the fragment SHALL be discarded
And the depth buffer SHALL NOT be written for that fragment

### Requirement: GBuffer Shading Model ID Encoding

The GBuffer pass MUST encode a 4-bit shading model ID per pixel. The deferred lighting pass MUST read this ID and dispatch to the correct BRDF evaluation path.

#### Scenario: Subsurface pixel uses wrap diffuse
Given a pixel written to GBuffer with `shadingModelID == 2` (Subsurface)
When the deferred lighting pass evaluates this pixel
Then the lighting SHALL use wrap diffuse (`(NdotL + wrap) / (1 + wrap)`) instead of standard Lambert
And the subsurface color SHALL contribute back-lighting from `saturate(-NdotL)` direction

#### Scenario: Unlit pixel skips all lighting
Given a pixel written to GBuffer with `shadingModelID == 0` (Unlit)
When the deferred lighting pass evaluates this pixel
Then the output color SHALL equal the GBuffer albedo multiplied by emissive intensity
And no diffuse, specular, shadow, AO, or IBL evaluation SHALL occur

#### Scenario: DefaultLit pixel uses existing BRDF
Given a pixel written to GBuffer with `shadingModelID == 1` (DefaultLit)
When the deferred lighting pass evaluates this pixel
Then the lighting SHALL use the existing Cook-Torrance GGX BRDF
And the result SHALL be pixel-identical to the pre-refactor output

### Requirement: Shader Permutation Key

The rendering system MUST define a `ShaderPermutationKey` as a 64-bit integer encoding material features. The key MUST encode at minimum: shadingModel (4 bits), blendMode (3 bits), bindless flag (1 bit), instanced flag (1 bit), POM flag (1 bit), materialGraph flag (1 bit).

#### Scenario: Two materials with same features share PSO
Given material A with shadingModel=DefaultLit, blendMode=Opaque, bindless=true
And material B with shadingModel=DefaultLit, blendMode=Opaque, bindless=true
When both materials request a PSO via `GetOrCreatePSO(key)`
Then the returned `RHIPipelineHandle` SHALL be identical for both calls
And the shader compiler SHALL be invoked exactly once

#### Scenario: Different shading models produce different PSOs
Given material A with shadingModel=DefaultLit
And material B with shadingModel=ClearCoat
When both materials request a PSO
Then the returned `RHIPipelineHandle` values SHALL differ
And each PSO SHALL contain the correct shader variant for its shading model

### Requirement: Shader Permutation Cache

The system MUST maintain a `ShaderPermutationCache` that maps `ShaderPermutationKey` to compiled `RHIPipelineHandle`. Cache misses MUST trigger on-demand shader compilation. The cache MUST support disk serialization for fast reload across sessions.

#### Scenario: PSO cache survives application restart
Given the cache has compiled a PSO for key K and serialized it to disk
When the application restarts and requests the PSO for key K
Then the PSO SHALL be loaded from disk without recompilation
And the loaded PSO SHALL produce identical rendering output

#### Scenario: PSO cache invalidates on shader source change
Given the cache has a PSO for key K compiled from shader hash H1
When the shader source is modified (new hash H2)
Then the cached PSO for key K SHALL be invalidated
And the next `GetOrCreatePSO(K)` call SHALL trigger recompilation

### Requirement: Unlit Shading Model

Materials with `shadingModel == EShadingModel::Unlit` MUST skip all lighting evaluation. The output color SHALL be `albedo * emissiveColor * emissiveIntensity`. No diffuse, specular, shadow, AO, or IBL contribution SHALL be applied.

#### Scenario: Unlit material in deferred pipeline
Given an opaque mesh with Unlit shading model and albedo=(1,0,0) and emissiveIntensity=2.0
When rendered through the deferred pipeline
Then the final pixel color SHALL be (2,0,0) regardless of light direction, intensity, or shadow state

### Requirement: Subsurface Shading Model

Materials with `shadingModel == EShadingModel::Subsurface` MUST use wrap diffuse lighting and a subsurface back-lighting term. The material MUST expose `subsurfaceColor` (float3) and `subsurfaceRadius` (float) parameters.

#### Scenario: Subsurface sphere shows back-lit color
Given a sphere with Subsurface shading model and subsurfaceColor=(1,0,0)
When lit from behind (light direction opposing the camera view)
Then pixels on the silhouette edge SHALL show red back-lighting contribution
And the diffuse wrap SHALL produce softer falloff than standard Lambert

### Requirement: Clear Coat Shading Model

Materials with `shadingModel == EShadingModel::ClearCoat` MUST evaluate dual-layer specular: a base GGX layer at the material roughness and a coating GGX layer at `clearCoatRoughness`. The coating layer SHALL use F0=0.04 (dielectric).

#### Scenario: Car paint shows dual specular highlights
Given a sphere with ClearCoat shading model, roughness=0.5, clearCoatRoughness=0.04
When lit by a single directional light
Then two distinct specular highlights SHALL be visible: a broad base highlight and a sharp coating highlight
And the coating highlight SHALL be visibly sharper than the base highlight

### Requirement: Cloth Shading Model

Materials with `shadingModel == EShadingModel::Cloth` MUST replace GGX specular with the Charlie/Ashikhmin sheen NDF. The material MUST expose `sheenColor` (float3) and `sheenRoughness` (float) parameters.

#### Scenario: Velvet sphere shows soft sheen
Given a sphere with Cloth shading model and sheenColor=(0.5,0.5,1.0)
When viewed at grazing angles
Then the sphere edges SHALL show a colored sheen brightening
And the specular highlights SHALL be softer and more diffuse than GGX

### Requirement: Two-Sided Foliage Shading Model

Materials with `shadingModel == EShadingModel::TwoSidedFoliage` MUST add a transmittance term for light passing through thin surfaces. The transmittance SHALL be `saturate(-NdotL) * subsurfaceColor * transmittanceStrength`.

#### Scenario: Leaf backlit by sun
Given a quad with TwoSidedFoliage shading model and subsurfaceColor=(0.2,0.8,0.1)
When the sun is behind the quad relative to the camera
Then the front-facing pixels SHALL show green-tinted back-lighting
And the back-lighting intensity SHALL be proportional to how directly the light aligns with the surface backside

### Requirement: Decal Component

The system MUST provide a `DecalComponent` that projects material onto existing GBuffer surfaces. The component MUST define: OBB projection volume, material reference, sort priority (int), fade distance, and fade angle.

#### Scenario: Decal projects albedo onto GBuffer
Given a DecalComponent with blend mode `Stain` positioned over a wall surface
When the Decal render pass executes after the GBuffer pass
Then the wall's GBuffer albedo SHALL be modified by the decal's texture
And the wall's GBuffer normal and roughness SHALL remain unchanged

#### Scenario: Decals sort by priority
Given decal A with priority=1 and decal B with priority=2 overlapping on the same surface
When both decals are rendered
Then decal B (higher priority) SHALL appear on top of decal A in the final GBuffer

### Requirement: Dithered Opacity

The GBuffer pass MUST support dithered opacity using a Bayer matrix pattern. When `useDitheredOpacity` is true, the shader SHALL discard fragments where the Bayer pattern value exceeds the material opacity. This MUST enable smooth LOD cross-fade transitions.

#### Scenario: LOD cross-fade uses screen-door pattern
Given two overlapping mesh LODs, LOD0 with opacity=0.7 and LOD1 with opacity=0.3, both with ditheredOpacity=true
When rendered in the GBuffer pass
Then approximately 70% of LOD0 pixels and 30% of LOD1 pixels SHALL survive the discard test
And the combined coverage SHALL approximate a smooth blend at screen resolution

### Requirement: Additional Texture Slots

The `MaterialInterface` MUST support the following additional texture slots: OpacityMask (R channel, for masked blend mode), Tangent (RGB, for hair anisotropic direction), SubsurfaceColor (RGB, spatially-varying SSS tint), ClearCoatNormal (RGB, separate normal for coating), Thickness (R channel, 0=thick 1=thin for SSS transmission), DetailNormal (RGB, second normal with UV1), DetailAlbedo (RGB, second albedo with UV1).

#### Scenario: Detail normal blends with base normal
Given a material with both a base normal map and a detail normal map
When the GBuffer pass samples both maps
Then the final normal SHALL be the Reoriented Normal Mapping (RNM) blend of base and detail normals
And the detail normal SHALL be sampled using UV1 (if available) or UV0 with detail tiling

## MODIFIED Requirements

### Requirement: materialFlags Encoding (MODIFIED)

The `materialFlags` field in `PerObjectCB` and `DrawCommand` SHALL be replaced by separate `shadingModel` (uint8), `blendMode` (uint8), and `materialDomain` (uint8) fields. The ORM packed bit (bit[2]) SHALL be preserved as a separate boolean field `useORM`.

#### Scenario: Legacy materialFlags migration
Given existing code that reads `materialFlags & 0x3u` for blend mode
When the refactoring is complete
Then all such code SHALL read from the new `blendMode` field instead
And the rendering output SHALL be pixel-identical to the pre-refactor state
