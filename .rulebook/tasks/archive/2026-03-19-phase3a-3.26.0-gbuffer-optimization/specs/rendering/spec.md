# Spec Delta: G-Buffer Optimization + Velocity Buffer

Applies to: `docs/specs/1.3-renderer.md`

## ADDED Requirements

### Requirement: Octahedral Normal Encoding
The G-Buffer system SHALL encode view-space normals using octahedral mapping into 2 bytes (RG8_UNORM) instead of R16G16B16A16_FLOAT. The implementation MUST use the Cigolle 2014 octahedral projection with lower-hemisphere wrapping, achieving ±0.002 maximum angular error.

#### Scenario: Normal encoding round-trip accuracy
Given a set of 10000 uniformly distributed unit normals on the sphere
When each normal is encoded via `OctEncode()` and decoded via `OctDecode()`
Then the maximum angular error between original and decoded normals SHALL be less than 0.5 degrees

#### Scenario: Normal encoding edge cases
Given normals pointing straight up (0,1,0), straight down (0,-1,0), and along each axis
When encoded and decoded
Then each reconstructed normal SHALL have dot product > 0.999 with the original

### Requirement: Optimized G-Buffer Layout
The deferred renderer SHALL use the following G-Buffer layout:
- RT0: R8G8B8A8_UNORM — RGB = albedo (sRGB linear), A = ambient occlusion
- RT1: R8G8B8A8_UNORM — RG = octahedral normal, B = metallic, A = roughness
- Depth: D32_FLOAT — hardware depth buffer

The system MUST NOT use R16G16B16A16_FLOAT for normal storage.

#### Scenario: G-Buffer bandwidth reduction
Given a 1920x1080 deferred render target allocation
When the G-Buffer is allocated with the optimized layout
Then the total G-Buffer size (excluding depth and velocity) SHALL be 8 bytes per pixel (2x R8G8B8A8_UNORM)

#### Scenario: G-Buffer clear values
Given frame begin
When the G-Buffer render targets are cleared
Then RT0 SHALL clear to (0, 0, 0, 1) representing black albedo with full AO
And RT1 SHALL clear to (0.5, 0.5, 0, 0) representing the default encoded up-normal with zero metallic and zero roughness

### Requirement: Velocity Buffer
The renderer SHALL produce a per-pixel velocity buffer in RG16F format containing screen-space motion vectors measured in pixels. The velocity buffer MUST be written during or immediately after the G-Buffer pass.

#### Scenario: Static camera produces zero velocity
Given a static camera with no object movement
When the velocity buffer is generated via depth reprojection
Then all pixels in the velocity buffer SHALL have values within ±0.001 of zero

#### Scenario: Camera rotation produces radial velocity pattern
Given a camera rotating around the Y-axis at constant angular velocity
When the velocity buffer is generated
Then the velocity magnitude SHALL increase with distance from the screen center
And the velocity direction SHALL be tangential to concentric circles around the screen center

#### Scenario: Per-object velocity override
Given a dynamic object with a known previous-frame world transform
When the vertex shader computes per-vertex velocity using current and previous MVP matrices
Then the velocity for that object's pixels SHALL reflect the object's motion, overriding camera-only reprojection

### Requirement: Previous Frame ViewProj Storage
The renderer SHALL store the previous frame's ViewProj matrix and provide it to shaders via constant buffer. The previous ViewProj MUST be updated at the end of each frame, after rendering is complete.

#### Scenario: First frame initialization
Given the first frame after engine startup where no previous ViewProj exists
When the velocity buffer is generated
Then the previous ViewProj SHALL be initialized to the current ViewProj, producing zero velocity

### Requirement: Velocity Debug Visualization
The renderer SHALL provide a debug visualization mode for the velocity buffer. The visualization MUST map velocity X to the red channel and velocity Y to the green channel, with zero velocity appearing as neutral gray (0.5, 0.5, 0).

#### Scenario: Debug velocity display toggle
Given the velocity debug visualization is enabled via debug flag
When the frame is rendered
Then the final output SHALL show velocity as color: `RGB = (vx * 0.5 + 0.5, vy * 0.5 + 0.5, 0)`

## MODIFIED Requirements

### Requirement: Deferred Lighting Pass Input
The deferred lighting pass SHALL read normals from RT1.rg using `OctDecode()`, metallic from RT1.b, roughness from RT1.a, and ambient occlusion from RT0.a. The lighting pass MUST NOT assume R16G16B16A16_FLOAT normal format.

#### Scenario: Lighting pass with packed G-Buffer
Given a scene with varied materials (metallic, rough, smooth, dielectric)
When the deferred lighting pass reads the optimized G-Buffer
Then the lit result SHALL be visually indistinguishable from the R16G16B16A16_FLOAT baseline (no banding, no artifacts on curved surfaces)

### Requirement: G-Buffer Debug Pass Compatibility
The G-Buffer debug visualization pass SHALL decode all channels from the new packed layout. Normal visualization MUST call `OctDecode()` on RT1.rg before display.

#### Scenario: Debug pass shows correct decoded normals
Given the G-Buffer debug pass is set to "normals" mode
When rendering a sphere with smooth normals
Then the debug output SHALL show smooth color gradients with no visible quantization artifacts
