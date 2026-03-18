# Rendering Parity Corrections — Spec Delta

## MODIFIED Requirements

### Requirement: BRDF Visibility Term Input
The BRDF evaluation in `pbr_common.hlsli` MUST pass `Pow4(roughness)` (roughness to the fourth power) as the `a2` parameter to `Vis_SmithJointApprox`, matching UE5 ShadingModels.ush:215. The current `roughness * roughness` input produces incorrect masking/shadowing that makes specular highlights too bright and too narrow on all surfaces.

#### Scenario: Roughness 0.5 Specular Width
Given a sphere with roughness=0.5 and metallic=1.0 under a single directional light
When the BRDF is evaluated per-pixel
Then the `a2` value passed to `Vis_SmithJointApprox` SHALL be `0.5^4 = 0.0625` (not `0.5^2 = 0.25`)
And the specular highlight SHALL be visibly wider than the pre-fix version

#### Scenario: Roughness 0.0 Mirror Reflection
Given a sphere with roughness=0.0
When the BRDF is evaluated
Then `a2 = 0.0^4 = 0.0` SHALL produce a point specular (same behavior as before — no change at roughness=0)

### Requirement: Fresnel F90 Micro-Occlusion
The `F_Schlick` evaluation in `EvaluateBRDF()` MUST use `F90 = saturate(50.0 * SpecularColor.g)` instead of a hardcoded `F90 = 1.0`, matching UE5 BRDF.ush:382. This prevents physically incorrect over-bright grazing reflections on materials with very low F0 (sub-2% specular).

#### Scenario: Low F0 Material at Grazing Angle
Given a material with F0 = 0.01 (very low specular) viewed at 85-degree grazing angle
When F_Schlick is evaluated
Then F90 SHALL be `saturate(50.0 * 0.01) = 0.5` (not 1.0)
And the grazing reflection SHALL be 50% of maximum instead of 100%

### Requirement: TAA Motion Vector Reprojection
The TAA resolve shader MUST compute per-pixel motion vectors by reprojecting world positions through the previous frame's view-projection matrix. The current stub implementation that returns `float2(0,0)` MUST be replaced with depth-based camera reprojection that reads the velocity buffer.

#### Scenario: Camera Rotation with Static Scene
Given a static scene with the camera rotating 5 degrees per frame
When TAA resolves the current frame
Then the history UV SHALL be offset by the computed motion vector (not zero)
And static objects SHALL NOT produce ghosting trails in the TAA output

#### Scenario: First Frame Without Previous ViewProj
Given the first frame of rendering where no previous ViewProj exists
When TAA resolves
Then the blend weight SHALL be 1.0 (use current frame only, no history)
And no ghosting or artifacts SHALL appear

### Requirement: SSR Output Without Fresnel Weighting
The SSR resolve shader MUST output unweighted scene color multiplied only by vignette, roughness fade, and intensity. Fresnel weighting SHALL NOT be applied in the SSR pass — it MUST be deferred to the deferred lighting pass where EnvBRDF is applied during SSR/IBL compositing. The invented `F0 = lerp(0.04, 0.8, metallic)` constant MUST be removed.

#### Scenario: Metallic Floor Reflection Brightness
Given a metallic floor (metallic=1.0, roughness=0.1) reflecting a white object
When SSR is composited with IBL in the deferred lighting pass
Then the SSR reflection brightness SHALL match the IBL reflection brightness within 10%
And the reflection SHALL NOT be squared-intensity dark

#### Scenario: roughnessFade Applied Once
Given a surface with roughness=0.3 and maxRoughness=0.4
When SSR trace and resolve execute
Then roughnessFade SHALL be applied exactly ONCE across both passes (not twice)
And the output alpha SHALL represent vignette confidence only

### Requirement: Vignette in HDR Space
The vignette darkening effect MUST be applied in HDR linear space BEFORE tonemapping, matching UE5 PostProcessTonemap.usf:515. Applying vignette after tonemapping (in LDR) produces incorrect tone curve response at screen edges.

#### Scenario: High Vignette Intensity
Given vignetteIntensity=0.8
When the tonemap pass executes
Then vignette SHALL multiply the HDR color BEFORE `color *= exposure` and BEFORE FilmToneMap
And the screen edge darkening SHALL go through the ACES tone curve (not bypass it)

### Requirement: Auto-Exposure Log2-Space Adaptation
The exposure temporal adaptation MUST operate in log2 space using UE5's dual exponential/linear model. The current linear-space single-exponential model produces perceptually non-uniform adaptation speeds.

#### Scenario: Dark to Bright Transition
Given a camera moving from a dark room (0.01 cd/m²) to bright outdoor (10000 cd/m²)
When exposure adapts over 2 seconds at SpeedUp=3.0 f-stops/sec
Then the adaptation SHALL progress through 6 f-stops in log2 space (perceptually uniform)
And SHALL NOT accelerate near the target as linear-space adaptation does

### Requirement: Per-Light SpecularScale and DiffuseScale Application
The deferred lighting shader MUST multiply the specular BRDF result by `light.specularScale` and the diffuse result by `light.diffuseScale` for each point and spot light, matching UE5 DeferredLightingCommon.ush:447-448.

#### Scenario: Light with SpecularScale=0
Given a point light with specularScale=0.0 and diffuseScale=1.0
When the deferred lighting evaluates this light
Then the specular contribution from this light SHALL be zero
And the diffuse contribution SHALL be normal (unchanged)

### Requirement: Motion Blur Neighbor Max Integration
The motion blur gather shader MUST read the neighbor max tile texture to determine per-tile maximum velocity for early-out optimization. Tiles with max velocity below 0.5 pixels SHALL skip the gather loop and output the center pixel color directly.

#### Scenario: Mostly Static Scene
Given a scene where 90% of tiles have zero velocity
When motion blur executes the TileMax, NeighborMax, and Gather passes
Then 90% of tiles SHALL early-out in the Gather pass (no gather samples taken)
And GPU time SHALL be measurably lower than without early-out

### Requirement: GTAO Medium Quality Tap Count
The GTAO compute shader MUST use `GTAO_NUM_TAPS=8` for medium quality, matching UE5 PostProcessAmbientOcclusion.usf:542 SHADER_QUALITY=2. The current value of 4 matches UE5's "very low" quality preset.

#### Scenario: AO Noise Comparison
Given the GTAO pass executing at full resolution
When NUM_TAPS is increased from 4 to 8
Then the raw AO output (before denoise) SHALL have visibly less noise
And the temporal stability SHALL improve (less flicker between frames)
