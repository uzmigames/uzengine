# Clustered Lights

## Purpose
The deferred lighting evaluates, in each pixel, only the local lights that can reach the pixel's
cluster, with the result of the per-pixel loop; the light components' parameters are honoured.

## ADDED Requirements

### Requirement: Cluster light lists
A compute pass SHALL assign every point and spot light of the frame to the clusters of the view
frustum (32-pixel tiles × 24 logarithmic depth slices) its range sphere touches, in the order the
per-pixel loop used, and the deferred lighting SHALL evaluate the lights of the pixel's cluster
(`r.Lights.Clustered`, default on), every light of the frame when the cluster overflowed its slots.

#### Scenario: Identity
Given the 80-light and 500-light reference scenes at frame 60
When the clustered frame is compared with the per-pixel loop's on the same binary
Then taa_output differs by at most 1e-5, also with 4 slots per cluster (every cluster overflows).

#### Scenario: Cost
Given the 500-light reference scene at 1920 × 1080
When the deferred lighting is timed with the lists and with the loop
Then the clustered pass costs at most half the loop and the cull pass at most 0.3 ms.

### Requirement: Light limits
The lighting constant buffer SHALL hold 512 point and 128 spot lights.

#### Scenario: Layout
Given the HLSL and C++ declarations of LightingCB
When their field offsets are compared (scripts/test_cbuffer_layouts.py)
Then every mapped field agrees and the struct is 43264 bytes.

### Requirement: Per-light parameters
The lighting SHALL apply the light's falloff exponent (when its inverse-square flag is clear), its
diffuse and specular scales and the point light's source radius, and the scene loader SHALL read them.

#### Scenario: Diffuse scale
Given one light over a rough floor with diffuseScale 0.5 and the same light with 1.0
When the floor radiance ratio is measured with the no-light scene's ambient subtracted
Then its median lies in [0.47, 0.55].

#### Scenario: Falloff exponent
Given one light with exponents 8 and 2 (inverse square off)
When the per-pixel ratio is compared with (1 − d²/r²)^6
Then the median absolute error is at most 0.02.

#### Scenario: Source radius
Given one light over a glossy floor with source radius 0, 20 and 50 cm
When the highlight's peak luminance is measured
Then it decreases with the radius and the 50 cm peak is at most 0.8 × the 0 cm peak.

### Requirement: Scenes with one light are unchanged
#### Scenario: Sponza
Given Sponza's fog project at frame 60 after a warm-up run
When compared with the committed build's warm capture
Then direct_light (the lighting's Lo) is identical and the colour lies within the cold/warm envelope
of block B.6 (the frame-60 GI state depends on the loading timing).
