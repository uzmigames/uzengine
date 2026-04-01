# Physics Queries & Character Controller — Specification

## ADDED Requirements

### Requirement: Raycast Queries
The physics system SHALL provide raycast queries that return hit information.

#### Scenario: Single Raycast Hit
Given a static box body at position (0,0,0) with half-extents (1,1,1)
When Raycast(origin={0,0,5}, direction={0,0,-1}, max_dist=10, layer_mask=ALL) is called
Then result.hit SHALL be true
And result.point SHALL be approximately (0, 0, 1) (top face of box)
And result.normal SHALL be (0, 0, 1)
And result.distance SHALL be approximately 4.0
And result.entity SHALL be the box entity

#### Scenario: Raycast Miss
Given no bodies along the ray path
When Raycast(origin={0,0,5}, direction={0,0,-1}, max_dist=2) is called
Then result.hit SHALL be false

#### Scenario: Multi-Hit Raycast
Given 3 bodies along a ray path at distances 2, 5, 8
When RaycastAll(origin, direction, max_dist=10) is called
Then the result SHALL contain 3 hits sorted by ascending distance

### Requirement: Shape Cast Queries
The physics system SHALL provide shape cast queries for swept volume collision detection.

#### Scenario: Sphere Cast
Given a wall at X=5
When SphereCast(origin={0,0,0}, direction={1,0,0}, radius=0.5, max_dist=10) is called
Then result.hit SHALL be true
And result.distance SHALL be approximately 4.5 (5.0 - radius)

#### Scenario: Box Cast
Given a narrow corridor
When BoxCast(origin, direction, half_extents={0.3,0.3,0.3}, rotation=identity, max_dist=10) is called
Then result SHALL detect the first wall surface hit

### Requirement: Overlap Queries
The physics system SHALL provide overlap queries returning all entities within a volume.

#### Scenario: Sphere Overlap
Given 5 bodies within radius=10 of point (0,0,0) and 3 bodies outside
When OverlapSphere(center={0,0,0}, radius=10, layer_mask=ALL) is called
Then the result SHALL contain exactly 5 entities
And results SHALL be capped at 256 per query

### Requirement: Character Controller
The physics system SHALL provide a character controller using JPH::CharacterVirtual.

#### Scenario: Grounded Detection
Given a CCharacterController standing on a flat surface
When CharacterMovementSystem ticks
Then is_grounded SHALL be true
And ground_normal SHALL be approximately (0,0,1)

#### Scenario: Slope Rejection
Given a CCharacterController on a surface with slope angle 60 degrees
When max_slope_angle is 50 degrees
Then the controller SHALL slide downhill
And is_grounded SHALL be false (too steep)

#### Scenario: Step Up
Given a CCharacterController walking toward a 0.25m step
When step_height is 0.35m
Then the controller SHALL smoothly step up over 2-3 frames
And position.z SHALL increase by 0.25

#### Scenario: Jump
Given a grounded CCharacterController
When Jump() is called with jump_speed=8.0
Then vertical velocity SHALL be set to 8.0 m/s upward
And is_grounded SHALL become false on next frame

#### Scenario: Moving Platform
Given a CCharacterController standing on a kinematic platform moving at velocity V
When the platform moves
Then the controller SHALL inherit the platform's velocity
And the controller SHALL remain on the platform without sliding

### Requirement: Lua Physics API
The scripting system SHALL expose physics queries and control to Lua.

#### Scenario: Lua Raycast
Given a Lua script calling uz.physics.raycast({0,0,5}, {0,0,-1}, 10)
When a body is hit
Then the result SHALL be a Lua table with fields: hit, point, normal, distance, entity
And point/normal SHALL be Vec3 userdata

#### Scenario: Lua Collision Callback
Given a Lua script calling uz.physics.on_collision(function(e) ... end)
When two bodies collide
Then the callback SHALL receive a table with entity_a, entity_b, point, normal, impulse

### Requirement: Physics Debug Draw
The physics system SHALL provide debug visualization toggled by CVar.

#### Scenario: Debug Draw Enabled
Given CVar physics.debug_draw = 1
When the frame renders
Then all active collider shapes SHALL be drawn as wireframes
And colors SHALL be: green=static, blue=dynamic, yellow=kinematic, red=sleeping, magenta=trigger

#### Scenario: Debug Draw Contact Points
Given CVar physics.debug_draw = 2
When bodies are in contact
Then contact points SHALL be drawn as small crosses
And contact normals SHALL be drawn as lines
