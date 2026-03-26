# VFX Advanced Modules, LOD, and Budget — Specification

## ADDED Requirements

### Requirement: Collision Module (Screen-Space)
The collision module SHALL detect particle-world collisions using depth buffer screen-space raycasts and respond with Kill, Bounce, or Slide behavior.

#### Scenario: Bounce Collision
Given a particle moving downward at velocity=[0,-10,0] and bounce_factor=0.6
When the particle crosses behind the scene depth buffer (collision detected)
Then velocity SHALL be reflected off the estimated surface normal
And velocity magnitude SHALL be multiplied by 0.6
And position SHALL be corrected to the collision surface + small offset

#### Scenario: Kill Collision
Given a particle with collision_response=Kill
When a collision is detected
Then the particle's age SHALL be set to lifespan (mark as dead)
And the particle index SHALL be recycled to the free list

#### Scenario: Slide Collision
Given a particle with collision_response=Slide and velocity=[5,-10,0] hitting a horizontal surface
When collision is detected
Then velocity SHALL be projected onto the surface plane
And the resulting velocity SHALL be approximately [5,0,0] (horizontal component preserved)

### Requirement: Light Render Module
The light render module SHALL emit dynamic point lights from particle positions with lifetime-based intensity curves.

#### Scenario: Per-Particle Light Creation
Given a LightRenderModule with radius=10.0 and intensity=2.0 and 30 alive particles
When the light update runs (CPU side, 1 frame latency)
Then up to 30 temporary point lights SHALL be created in the light system
And each light SHALL be positioned at its particle's position
And each light SHALL have color from particle color × intensity_over_life curve
And each light SHALL have the configured radius and intensity

#### Scenario: Max Light Cap
Given a LightRenderModule with max_lights=64 and 200 alive particles
When the readback buffer is filled
Then only the 64 closest-to-camera particles SHALL have lights created
And the remaining 136 particles SHALL not emit lights

### Requirement: Mesh Render Module
The mesh render module SHALL render 3D mesh instances at particle positions using GPU instancing.

#### Scenario: Instanced Mesh Rendering
Given a MeshRenderModule with a cube mesh and 500 alive particles
When the mesh render pass executes
Then 500 instances SHALL be drawn using DrawIndexedInstancedIndirect
And each instance transform SHALL include particle position, rotation, and mesh_scale
And the draw call count SHALL be 1 (single instanced draw)

#### Scenario: Velocity-Aligned Rotation
Given a MeshRenderModule with rotation_mode=VelocityAligned
When an instance transform is computed for a particle with velocity=[5,0,5]
Then the mesh SHALL be rotated to face the velocity direction [5,0,5]
And the forward axis of the mesh SHALL align with normalized velocity

### Requirement: LOD System
The VFX system SHALL reduce particle quality based on distance to camera and screen coverage.

#### Scenario: Distance-Based LOD
Given an emitter at distance 120m from camera with lod_distances=[50, 100, 200]
When VFXLodEvaluator computes LOD level
Then the LOD level SHALL be Minimal (distance > 100m threshold for Reduced→Minimal)
And the spawn rate multiplier SHALL be 0.25 (25% of base rate)

#### Scenario: Screen-Size Culling
Given an emitter with bounding radius=5m at distance=500m and screen_height=1080
When screen coverage is computed
Then screen_pixels SHALL be approximately 5.4 pixels
And if min_screen_pixels=4, the emitter SHALL NOT be culled

#### Scenario: Full Culling
Given an emitter at distance 250m with lod_distances=[50, 100, 200]
When VFXLodEvaluator computes LOD level
Then the LOD level SHALL be Culled (distance > 200m)
And no particles SHALL be simulated or rendered

### Requirement: Significance Manager
The VFX system SHALL rank emitters by significance score and cull lowest-significance emitters when the global budget is exceeded.

#### Scenario: Significance Scoring
Given emitter A (priority=80, distance=50m, visible) and emitter B (priority=20, distance=200m, not visible)
When significance scores are computed
Then emitter A score SHALL be significantly higher than emitter B
And the formula SHALL be: priority × (1 / (1 + distance × 0.01)) × visibility_factor

#### Scenario: Budget-Driven Culling
Given total demand=800K particles and budget=500K
When the significance manager enforces budget
Then emitters SHALL be iterated from lowest significance
And their spawn rates SHALL be reduced (LOD downgrade) or disabled
Until total demand fits within 500K budget

### Requirement: Quality-Scaled Budget
The particle budget SHALL scale with the engine's quality level setting.

#### Scenario: Quality Level Change
Given the engine quality is set to "Medium"
When ParticleBudget initializes
Then max_total_particles SHALL be 250000
And this SHALL be read from the .uzproj VFX quality settings

#### Scenario: Quality Levels
Given the quality level enum
Then Low SHALL map to 125000 max particles
And Medium SHALL map to 250000
And High SHALL map to 500000
And Ultra SHALL map to 1000000
