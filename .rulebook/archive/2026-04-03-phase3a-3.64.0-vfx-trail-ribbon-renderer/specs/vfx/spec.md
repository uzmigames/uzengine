# VFX Trail/Ribbon Renderer — Specification

## ADDED Requirements

### Requirement: Trail Point Collection
The trail system SHALL collect entity positions over time as trail points in a ring buffer, respecting minimum distance thresholds.

#### Scenario: Add Trail Point
Given a TrailComponent with min_point_distance=0.5 and last point at [0,0,0]
When the entity moves to [0,0,0.6] (distance=0.6 > 0.5)
Then a new TrailPoint SHALL be added at [0,0,0.6]
And the trail point count SHALL increase by 1

#### Scenario: Skip Nearby Point
Given a TrailComponent with min_point_distance=0.5 and last point at [0,0,0]
When the entity moves to [0,0,0.3] (distance=0.3 < 0.5)
Then no new trail point SHALL be added
And the trail point count SHALL remain unchanged

#### Scenario: Point Expiry
Given a trail point with age=5.0 and point_lifetime=5.0
When TrailComponent::OnTick(dt=0.016) runs
Then the point SHALL be removed from the ring buffer
And the trail segment connected to this point SHALL no longer render

### Requirement: Camera-Facing Ribbon Geometry
The trail renderer SHALL generate camera-facing quad strips between adjacent trail points with configurable width.

#### Scenario: Geometry Generation
Given 5 trail points forming a curve
When TrailMeshBuilder generates geometry
Then 8 vertices SHALL be created (2 per point, minus endpoints special case)
And 12 triangles (24 indices) SHALL be created (2 triangles per segment × 4 segments)
And each vertex pair SHALL face the camera (billboard along trail tangent)

#### Scenario: Width Curve Application
Given a trail with width_start=2.0, width_end=0.0, and linear width_curve
When geometry is generated for a point at normalized_distance=0.5 (midpoint)
Then the width at this point SHALL be approximately 1.0
And left/right vertices SHALL be offset by ±0.5 from the trail center

### Requirement: UV Mapping and Scrolling
The trail system SHALL map UVs along the ribbon with optional V-axis scrolling for animated textures.

#### Scenario: UV Layout
Given a trail with 10 points spanning total distance 5.0
When UVs are generated
Then U coordinate SHALL be 0 on the left edge and 1 on the right edge
And V coordinate SHALL be 0 at the head (newest point) and 1 at the tail (oldest point)
And V SHALL be proportional to accumulated distance along the trail

#### Scenario: UV Scrolling
Given a trail with uv_scroll_rate=2.0 and time=3.0
When the pixel shader samples the texture
Then V coordinate SHALL be offset by time × uv_scroll_rate = 6.0 (wrapped via frac())

### Requirement: Color Over Distance
The trail system SHALL support color curves that modulate trail color based on distance from head.

#### Scenario: Trail Color Fade
Given a trail with color_over_distance curve: [{t:0, value:[1,1,1,1]}, {t:1, value:[1,1,1,0]}]
When a vertex at normalized_distance=0.75 is generated
Then vertex color SHALL be approximately [1, 1, 1, 0.25]
And the pixel shader SHALL multiply texture color by this vertex color

### Requirement: Dynamic Vertex Buffer Updates
The trail system SHALL update vertex and index buffers each frame without causing CPU-GPU pipeline stalls.

#### Scenario: Frame Update
Given 50 active trails with varying point counts
When TrailSystem::Update(dt) runs
Then all trail geometries SHALL be regenerated
And vertex data SHALL be uploaded via ring-buffer allocator (no wait)
And the previous frame's buffers SHALL remain valid until GPU completes

### Requirement: Trail Transparency Sorting
The trail render pass SHALL sort trail segments for correct transparency compositing.

#### Scenario: Overlapping Trails
Given trail A at distance 10.0 from camera and trail B at distance 5.0
When the trail render pass executes
Then trail A SHALL render first (farther = first in back-to-front order)
And trail B SHALL render second (closer = on top)

### Requirement: Trail Asset Format
The trail system SHALL load .uztrail JSON files defining trail visual properties.

#### Scenario: Load .uztrail Asset
Given a valid .uztrail JSON with material, width_start, width_end, width_curve, uv_scroll_rate
When TrailAsset::Load(path) is called
Then all properties SHALL be parsed correctly
And referenced curve assets SHALL be loaded
And the asset SHALL be ready for use by TrailComponent
