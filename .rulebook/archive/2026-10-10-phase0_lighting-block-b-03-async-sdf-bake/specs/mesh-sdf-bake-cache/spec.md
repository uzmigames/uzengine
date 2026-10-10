# Mesh SDF Bake Cache

## Purpose
Mesh signed distance fields for the GI are produced once per mesh content and resolution, reused
from disk, and produced in the background when missing, without stalling the frame.

## ADDED Requirements

### Requirement: Cached equals baked
The system SHALL store a baked mesh SDF with its triangle/BVH data under a key of mesh content,
resolution, sidedness and baker version, and a cache hit SHALL yield the same bits as a fresh bake.

#### Scenario: Warm load of Sponza
Given Sponza was loaded once with the cache enabled
When it is loaded again
Then every mesh SDF comes from the cache, is bit-identical to the baked volume, and the Camera C
lighting is identical to the cold load.

### Requirement: Bakes do not stall the frame
The system SHALL run the CPU part of a bake off the main thread and SHALL complete the GPU part by
fence, marking the SDF valid only after the fence, without issuing `WaitIdle` inside the render loop.

#### Scenario: GI enabled at runtime
Given a scene loaded without SDF GI
When `r.RadianceCache` is enabled
Then the missing SDFs are baked in the background, no frame exceeds twice the median during the
bake, and the GI appears once the bakes complete.

### Requirement: Corrupt entries are harmless
The system SHALL treat an unreadable or stale cache entry as a miss and rewrite it.

#### Scenario: Poisoned cache file
Given a cache entry was truncated on disk
When the mesh loads
Then the SDF is baked, the entry is rewritten, and the lighting equals the cold load.
