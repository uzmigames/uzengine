# DDGI and SDF probe writers compared on the current scene set

## Purpose
DDGI and SDF probe writers compared on the current scene set.

## ADDED Requirements

### Requirement: DDGI and SDF probe writers compared on the current scene set
The DDGI writer SHALL trace the same occluder set as the distance field.

#### Scenario: The gate
Given Sponza with its mesh decals
When the TLAS is rebuilt
Then no translucent draw or mesh decal is an instance.
