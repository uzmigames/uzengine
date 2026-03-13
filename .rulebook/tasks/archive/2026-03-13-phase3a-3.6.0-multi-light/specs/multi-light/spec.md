# Multi-Light Shading — Spec Delta

## ADDED Requirements

### Requirement: Light GPU Structure
The engine MUST define a LightGPU struct containing type (directional/point/spot), world position, direction, color, intensity, range, inner cone angle, and outer cone angle, padded to 16-byte alignment for HLSL compatibility.

#### Scenario: Point Light Fields
Given a LightGPU configured as a point light
When uploaded to the GPU
Then position, color, intensity, and range MUST be valid
And direction and cone angles MUST be ignored by the shader

#### Scenario: Spot Light Fields
Given a LightGPU configured as a spot light
When uploaded to the GPU
Then all fields (position, direction, color, intensity, range, innerCone, outerCone) MUST be valid
And innerCone MUST be less than or equal to outerCone

### Requirement: Per-Frame Light Array
The PerFrameCB MUST contain a light count field and an array of up to 8 LightGPU entries, maintaining D3D12 256-byte CB alignment.

#### Scenario: Zero Lights
Given lightCount is 0
When the pixel shader evaluates lighting
Then only ambient color SHALL contribute to the final radiance

#### Scenario: Maximum Lights
Given 12 lights exist in the scene
When the CPU culling pass selects lights
Then at most 8 lights SHALL be uploaded to the CB
And selection SHALL prioritize by screen-space contribution (intensity / distance^2)

### Requirement: Point Light Attenuation
The pixel shader MUST evaluate point lights using physically-based inverse-square falloff with a smooth boundary window function that reaches zero at the light's range.

#### Scenario: Attenuation at Range Boundary
Given a point light with range R
When a fragment is at distance R from the light
Then the attenuation factor MUST be 0.0

#### Scenario: Attenuation at Half Range
Given a point light with range R and intensity I
When a fragment is at distance R/2
Then the attenuation MUST be greater than 0.0 and follow inverse-square law with window

### Requirement: Spot Light Cone Falloff
The pixel shader MUST evaluate spot lights with angular falloff between inner and outer cone angles, multiplied by the point light distance attenuation.

#### Scenario: Inside Inner Cone
Given a spot light with innerCone = 15 degrees
When a fragment is within 10 degrees of the spot direction
Then the angular attenuation factor MUST be 1.0

#### Scenario: Outside Outer Cone
Given a spot light with outerCone = 30 degrees
When a fragment is at 35 degrees from the spot direction
Then the angular attenuation factor MUST be 0.0

#### Scenario: Between Cones
Given a spot light with innerCone = 15 and outerCone = 30 degrees
When a fragment is at 22.5 degrees from the spot direction
Then the angular factor MUST be between 0.0 and 1.0 (smooth interpolation)

### Requirement: CPU Light Culling
The renderer MUST perform CPU-side light culling each frame, sorting visible lights by estimated screen contribution and selecting the top N (max 8) for GPU upload.

#### Scenario: Distant Lights Culled
Given 10 point lights where 3 are beyond camera far plane
When culling executes
Then the 3 distant lights SHALL NOT be included in the GPU light array
