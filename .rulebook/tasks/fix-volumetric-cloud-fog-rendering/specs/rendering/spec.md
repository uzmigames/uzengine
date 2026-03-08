# Delta for Volumetric Cloud & Fog Rendering

## MODIFIED Requirements

### Requirement: Volumetric Cloud Raymarching
The cloud renderer SHALL use adaptive-count raymarching through a bounded atmospheric slab with exponentially distributed step sizes to eliminate visible banding and slicing artifacts.

#### Scenario: Raymarch step distribution eliminates banding
Given a camera looking at the cloud layer at the horizon
When the cloud shader executes at MEDIUM quality (64 steps)
Then no horizontal banding or slicing artifacts are visible
And step sizes are smaller near the ray entry point and larger toward the exit

#### Scenario: Quality levels control step count
Given the cloud quality is set to LOW
When the cloud shader executes
Then the maximum raymarch step count SHALL be 32
And when quality is MEDIUM the step count SHALL be 64
And when quality is HIGH the step count SHALL be 96
And when quality is CINEMATIC the step count SHALL be 128

#### Scenario: Blue noise jitter removes per-pixel patterns
Given the cloud shader is rendering a frame
When per-pixel jitter is applied to the raymarch start position
Then the jitter offset MUST use a quasi-random blue noise distribution (R2 sequence or equivalent)
And the jitter pattern MUST vary across both pixel position and frame index

### Requirement: Cloud Density Field Sampling
The cloud density field SHALL use multi-octave fractal noise with smooth interpolation to produce natural, continuous cloud shapes without quantized or layered appearance.

#### Scenario: FBM produces smooth cloud shapes
Given the cloud density sampling function is called
When evaluating density at a world position
Then the base shape noise MUST use at least 4 octaves of gradient noise (FBM)
And detail erosion noise MUST use at least 2 octaves
And all noise interpolation MUST use C2-continuous (quintic Hermite) blending

#### Scenario: Height gradient produces smooth boundaries
Given a sample point at the cloud layer boundary (top or bottom)
When the height gradient is evaluated
Then density MUST fade smoothly to zero using a cosine or smooth Hermite curve
And there SHALL be no visible hard edges at cloud top or bottom boundaries

#### Scenario: Spatial coverage variation
Given the cloud coverage parameter
When cloud density is sampled across the cloud layer
Then coverage MUST vary spatially using a low-frequency 2D noise pattern
And the result SHALL NOT be uniform coverage across the entire sky

### Requirement: Volumetric Light Transport
The cloud lighting model SHALL use energy-conserving Beer-Lambert transmittance with dual-lobe Henyey-Greenstein phase function and multi-scattering approximation.

#### Scenario: Transmittance integration is energy-conserving
Given a raymarch step with non-zero cloud density
When the scattered light contribution is computed
Then the integration MUST use the form `(scatter - scatter * stepTransmittance) / extinction * transmittance`
And total energy MUST NOT exceed the incoming light energy

#### Scenario: Sun self-shadowing with adequate samples
Given a cloud sample point receiving direct sun light
When sun transmittance is computed by marching toward the light
Then at least 8 shadow samples MUST be taken with logarithmic step spacing
And the shadow ray MUST be bounded to the cloud layer slab

#### Scenario: Multi-scattering approximation
Given a dense cloud region with high optical depth
When lighting is computed
Then a multi-scattering term MUST be added to approximate light bouncing within the cloud
And the result SHALL prevent unnaturally dark cloud interiors

## ADDED Requirements

### Requirement: Temporal Accumulation for Clouds
The cloud renderer SHALL support temporal accumulation that reprojects the previous frame's cloud result to amortize rendering cost and reduce noise.

#### Scenario: Temporal reprojection blends frames
Given temporal accumulation is enabled
When a new frame is rendered
Then the cloud result from the previous frame MUST be reprojected using motion vectors
And blended with the current frame at a configurable ratio (default 95% history, 5% current)

#### Scenario: History rejection prevents ghosting
Given the camera moves rapidly or an object becomes disoccluded
When the reprojected history pixel differs significantly from the current neighborhood
Then the history MUST be clamped to the min/max of the current frame's 3x3 neighborhood
And the blend factor MUST shift toward the current frame

#### Scenario: Camera cut invalidates history
Given the camera teleports or the scene changes
When the history valid flag is false
Then temporal accumulation MUST use 100% current frame (no history blending)
And the history valid flag MUST be set to true after the first resolved frame

### Requirement: Integrated Volumetric Fog
The volumetric fog system SHALL use analytical height-fog integration along the view ray with proper inscattering, blending smoothly with both the scene and the sky.

#### Scenario: Analytical height fog integration
Given fog parameters (density, height falloff, base height)
When fog is computed for a scene pixel
Then the fog amount MUST be computed using the Frostbite 2014 analytical integral formula
And the result SHALL NOT be a simple flat screen-space overlay

#### Scenario: Fog blends smoothly with sky
Given a pixel at the sky boundary (depth near 1.0)
When fog is applied
Then fog density MUST smoothly transition to zero at sky depth
And there SHALL be no visible hard edge between fogged geometry and sky

#### Scenario: Fog inscattering matches cloud phase
Given fog inscattering from the directional light
When the inscatter contribution is computed
Then a Henyey-Greenstein phase function MUST be used
And the phase parameters SHOULD be consistent with the cloud lighting model

### Requirement: Cloud Quality Scalability
The cloud renderer SHALL support configurable quality levels that control performance vs. visual quality tradeoffs.

#### Scenario: Quality levels meet performance targets
Given the cloud renderer is running at 1080p resolution
When cloud quality is set to LOW
Then GPU time for the cloud pass MUST be under 2ms
And when quality is MEDIUM, GPU time MUST be under 3ms
And when quality is HIGH, GPU time MUST be under 5ms

#### Scenario: Quality level is configurable via scene and IPC
Given a .uzscene file with `cloud_quality = HIGH`
When the scene is loaded
Then the renderer MUST set the cloud quality to HIGH
And the quality MUST be changeable at runtime via `renderer.setCloudQuality` IPC command

### Requirement: Cloud Debug Visualization
The cloud renderer SHALL provide debug visualization modes for density, step count, transmittance, and lighting contribution.

#### Scenario: Debug mode shows density field
Given cloud debug mode is set to 1 (density)
When the cloud shader renders
Then the output MUST be a grayscale visualization of raw cloud density
And no lighting or scattering SHALL be applied

#### Scenario: Debug mode shows step count heatmap
Given cloud debug mode is set to 2 (step count)
When the cloud shader renders
Then the output MUST be a color-coded heatmap of raymarch iterations per pixel
And blue MUST represent few steps and red MUST represent many steps

### Requirement: Volumetric Test Scene
The project SHALL include a dedicated test scene for visual validation of the volumetric cloud and fog system.

#### Scenario: Test scene loads with volumetric effects
Given the file `assets/scenes/volumetric_test.uzscene` exists
When the scene is loaded in the editor
Then clouds MUST be visible with the configured cloud layer
And fog MUST be active with smooth height-based falloff
And a directional light MUST illuminate the clouds
