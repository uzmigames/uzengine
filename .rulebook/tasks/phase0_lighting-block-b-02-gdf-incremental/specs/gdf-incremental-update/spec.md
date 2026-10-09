# GDF Incremental Update

## Purpose
The global distance field follows the camera and the scene by recomposing only what changed, and
the GI consumers lose history only where the field changed.

## ADDED Requirements

### Requirement: Scroll equals recompose
The system SHALL produce, after a recentre, a field equal to a full recompose on every overlapping
voxel.

#### Scenario: One-voxel recentre
Given a clipmap recentres by one voxel on any axis
When the field is compared with a full recompose from the same instances
Then every overlapping voxel is bit-identical and only the entering slabs were dispatched.

### Requirement: Local changes stay local
The system SHALL recompose only the regions overlapping an instance's old and new bounds (plus the
distance band) when that instance moves, appears, disappears or changes material, and SHALL NOT
discard GI history outside those regions.

#### Scenario: A box moves across the floor
Given a Movable box moves while the rest of the scene is Static
When frames are rendered
Then the radiance cache logs no global invalidation, the indirect light under the box follows it
within the registered frame budget, and the lighting elsewhere is unchanged.

### Requirement: Static scenes pay nothing
The system SHALL skip the instance diff for Static instances and keep Sponza's lighting
bit-identical to the full-recompose path.

#### Scenario: Sponza Camera C
Given the Sponza scene with every instance Static
When the Camera C ROIs are compared with the full-recompose build
Then the ROI means are identical to within run-to-run noise.
