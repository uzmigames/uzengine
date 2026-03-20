# Global Illumination System — Spec Delta

## ADDED Requirements

### Requirement: Mesh SDF Generation
The engine MUST generate a signed distance field (3D texture, R16F) for each static mesh, representing the minimum signed distance from any point to the mesh surface. Resolution SHALL be configurable (32^3 to 64^3 voxels). SDF MUST be cached to disk as `.uzsdf` alongside the `.uzmesh`.

#### Scenario: SDF for unit cube
Given a 1m cube mesh centered at origin
When the SDF generator processes the mesh at 32^3 resolution
Then the resulting 3D texture contains negative values inside the cube, zero at the surface, and positive values outside
And the distance error at any point is less than 1 voxel width

#### Scenario: SDF cache hit
Given a mesh with an existing `.uzsdf` file on disk
When the mesh is loaded at runtime
Then the SDF is loaded from disk without re-generation
And the load time is less than 5ms for a 64^3 SDF

### Requirement: Global Distance Field
The engine MUST maintain a global distance field (4 cascaded 3D textures) centered on the camera, composited from all mesh SDFs in the scene. Each cascade SHALL be 128^3 voxels. The cascades MUST scroll incrementally as the camera moves.

#### Scenario: Camera movement cascade scroll
Given the camera moves 2 meters in world space
When the global SDF update executes
Then only the border voxels exposed by the movement are re-computed
And the center of each cascade tracks the camera position

#### Scenario: Multi-mesh composite
Given two meshes (sphere and cube) placed 3 meters apart
When the global SDF is sampled at a point between them
Then the returned distance is the minimum distance to either mesh surface

### Requirement: SDF Ray Tracing
The engine MUST provide a compute shader that traces rays against the global distance field using sphere marching. Each ray SHALL march up to 64 steps with an epsilon of 0.5cm. The tracer MUST return hit position, hit normal (from SDF gradient), and hit distance.

#### Scenario: Ray hits visible geometry
Given a ray cast from camera position toward a mesh 5 meters away
When the SDF ray tracer executes
Then a hit is reported at approximately 5 meters distance
And the hit normal matches the mesh surface normal at the hit point

#### Scenario: Ray misses all geometry
Given a ray cast toward empty sky
When the SDF ray tracer executes 64 steps
Then no hit is reported
And the ray is marked as a miss

### Requirement: Radiance Cache
The engine MUST maintain a 3D grid of irradiance probes in world space. Each probe SHALL store L2 SH coefficients (9 float3). Probes MUST be updated each frame by tracing rays from probe positions through the SDF and accumulating irradiance. The update MUST use temporal accumulation (exponential moving average).

#### Scenario: Probe near red wall
Given a probe positioned 1 meter from a red wall (albedo = 1, 0, 0)
When 64 rays are traced from the probe and accumulated over 10 frames
Then the probe's SH coefficients show red bias in the hemisphere facing the wall
And the SH L0 coefficient is non-zero in the R channel

#### Scenario: Probe update budget
Given 8192 active probes in the scene
When the radiance cache update executes
Then at most 512 probes are updated per frame (round-robin)
And the GPU time for the update is less than 1ms at 1080p

### Requirement: Probe Interpolation with Visibility
The deferred lighting shader MUST sample the radiance cache via trilinear interpolation of the 8 nearest probes. Visibility MUST be tested via Chebyshev inequality (mean depth + depth variance) to prevent light leaking through walls.

#### Scenario: Light leak prevention
Given a probe on one side of a thin wall and a shading point on the other side
When the deferred lighting shader samples the radiance cache
Then the Chebyshev visibility test rejects the occluded probe
And the shading point does not receive light from the other side of the wall

### Requirement: Three-Tier GI Compositing
The deferred lighting shader MUST composite GI from three tiers: SSGI for near-field (0-5m), radiance cache for mid-field (5-50m), and SkyLight SH for far-field (50m+). Blending between tiers MUST use smooth distance-based weights.

#### Scenario: Indoor corner
Given a pixel at a wall-floor corner with nearby geometry (< 2m)
When the deferred lighting shader evaluates GI
Then the SSGI near-field contribution dominates (weight > 0.7)
And color bleeding from the nearby wall is visible

#### Scenario: Outdoor open field
Given a pixel on a flat ground plane with no nearby geometry
When the deferred lighting shader evaluates GI
Then the radiance cache mid-field contribution dominates
And the fill light matches the sky hemisphere average irradiance
