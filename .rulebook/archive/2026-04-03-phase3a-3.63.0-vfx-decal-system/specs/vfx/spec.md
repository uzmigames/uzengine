# VFX Decal System — Specification

## ADDED Requirements

### Requirement: Deferred Decal Rendering
The decal system SHALL render decals by modifying the G-Buffer in a deferred pass after geometry rendering and before lighting.

#### Scenario: Decal Placement on Surface
Given a DecalComponent at position=[5,2,0] facing normal=[0,0,1] with half_extents=[0.5,0.5,0.2]
When the deferred decal pass renders
Then pixels within the decal's box projection volume SHALL sample the decal texture
And pixels outside the box SHALL be discarded (not affected)
And backfacing pixels (dot(surface_normal, decal_forward) < 0.1) SHALL be discarded

#### Scenario: G-Buffer Modification
Given a decal with albedo, normal, and roughness textures
When the decal pixel shader processes a valid pixel
Then G-Buffer albedo SHALL be blended according to blend_mode
And G-Buffer normal SHALL be lerp(surface_normal, decal_normal, normal_intensity)
And G-Buffer roughness SHALL be overridden if roughness_override >= 0

### Requirement: Decal Blend Modes
The decal system SHALL support four blend modes for compositing decal color with surface color.

#### Scenario: Opaque Blend
Given a decal with blend_mode=Opaque
When a pixel is inside the decal box
Then the final color SHALL be decal_color (full replacement)

#### Scenario: Additive Blend
Given a decal with blend_mode=Additive
When a pixel is inside the decal box
Then the final color SHALL be surface_color + decal_color

#### Scenario: Multiply Blend
Given a decal with blend_mode=Multiply
When a pixel is inside the decal box
Then the final color SHALL be surface_color * decal_color

#### Scenario: Screen Blend
Given a decal with blend_mode=Screen
When a pixel is inside the decal box
Then the final color SHALL be 1 - (1 - surface_color) * (1 - decal_color)

### Requirement: Decal Lifetime and Fade
The decal system SHALL automatically fade and remove decals based on configurable lifetime and fade curves.

#### Scenario: Fade Over Lifetime
Given a decal with lifetime=10.0 and a linear fade_curve (1→0 over [0,1])
When 5 seconds have elapsed (age=5.0, age/lifetime=0.5)
Then fade_alpha SHALL be approximately 0.5
And decal opacity SHALL be multiplied by fade_alpha

#### Scenario: Expired Decal Removal
Given a decal with lifetime=10.0 and age=10.0
When DecalManager::Cleanup() runs
Then the decal SHALL be returned to the pool
And the decal SHALL no longer render

### Requirement: Decal Object Pool
The decal system SHALL use an object pool to avoid runtime allocation and deallocation overhead.

#### Scenario: Pool Acquisition
Given a DecalPool with pool_size=1000 and 500 in use
When AcquireDecal(asset) is called
Then an available DecalComponent SHALL be returned
And in_use_count SHALL increase to 501

#### Scenario: Pool Exhaustion
Given a DecalPool with pool_size=1000 and 1000 in use
When AcquireDecal(asset) is called
Then nullptr SHALL be returned
And UZ_LOG(VFX, Warning) SHALL report pool exhaustion

### Requirement: Decal Texture Atlasing
The decal system SHALL support texture atlasing to reduce GPU state changes during rendering.

#### Scenario: Atlas Packing
Given 10 decal textures of 512×512 pixels
When AddDecal() is called for each
Then all 10 SHALL be packed into a single 2048×2048 atlas texture
And each entry SHALL have correct uv_min and uv_max coordinates
And the atlas SHALL be accessible as a single RHITextureHandle

### Requirement: Decal Sorting
The decal system SHALL sort decals by priority and distance to ensure correct visual layering.

#### Scenario: Sort Order Priority
Given decal A (sort_order=0) and decal B (sort_order=1) overlapping
When the deferred decal pass renders
Then decal A SHALL render first
And decal B SHALL render second (on top of A)

### Requirement: Decal Frustum Culling
The decal system SHALL skip rendering of decals outside the camera frustum.

#### Scenario: Off-Screen Decal
Given a decal at position=[1000,0,0] and camera looking at [0,0,0]
When FrustumCull(camera) is called
Then the decal SHALL be marked as not visible
And the deferred pass SHALL skip rendering this decal
