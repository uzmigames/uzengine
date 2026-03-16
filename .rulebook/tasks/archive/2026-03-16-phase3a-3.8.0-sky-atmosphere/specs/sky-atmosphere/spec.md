# Sky & Atmosphere — Spec Delta

## ADDED Requirements

### Requirement: Physical Atmosphere Model
The sky renderer MUST implement Bruneton-style single-scattering with Rayleigh and Mie terms, producing physically plausible sky colors for any sun elevation angle from -10 to +90 degrees.

#### Scenario: Midday Sky
Given sun elevation at 60 degrees with Earth-like atmosphere parameters
When the sky is rendered
Then the zenith SHALL be deep blue (Rayleigh-dominated)
And the horizon SHALL be lighter blue with slight desaturation

#### Scenario: Sunset Sky
Given sun elevation at 2 degrees
When the sky is rendered
Then the horizon near the sun SHALL be orange/red (long path Rayleigh extinction)
And the opposite horizon SHALL remain blue-purple

### Requirement: Exponential Height Fog
The engine MUST support exponential height fog with configurable density, color, height falloff coefficient, base height, and max opacity, composited after scene lighting.

#### Scenario: Fog at Ground Level
Given fog with density 0.02, base height 0, falloff 0.5
When a fragment is at height 0 and distance 100 from camera
Then the fog factor SHALL be significant (visible fog blending)

#### Scenario: Fog Above Falloff
Given fog with base height 0 and falloff 2.0
When a fragment is at height 50 and distance 100
Then the fog factor SHALL be near zero (exponential decay with height)

### Requirement: Sky-to-IBL Capture
The engine MUST capture the rendered sky atmosphere into a cubemap suitable for the IBL pipeline (3.7.0), re-baking when sun direction or atmosphere parameters change.

#### Scenario: Sun Direction Change
Given a SkyAtmosphere with IBL bake active
When the sun direction changes by more than 1 degree
Then the sky cubemap SHALL be re-baked
And the irradiance and prefiltered maps SHALL be updated

### Requirement: Sky Pass Ordering
The sky pass MUST render after the geometry pass and before post-processing, using reverse-Z depth test to only fill pixels not covered by scene geometry.

#### Scenario: Sky Behind Geometry
Given a scene with opaque meshes and sky atmosphere
When rendered
Then sky pixels SHALL only appear where no geometry was drawn
And geometry SHALL never be overwritten by sky
