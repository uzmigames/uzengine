# Mesh winding

## Purpose
Every sample mesh is wound for the engine's clockwise-front rasterizer, so back-face culling keeps the
side the normals point to.

## ADDED Requirements

### Requirement: Sample meshes are wound clockwise-front
Every `.uzmesh` under the sample content SHALL have at most 1 % of its LOD0 triangles whose winding
disagrees with their vertex normals, except meshes whose residue is the LOD simplifier's (reported);
`scripts/rendering/repair_mesh_winding.py` SHALL audit this and SHALL flip whole-mesh inversions on request.

#### Scenario: The audit
Given the sample content
When the audit runs
Then no mesh is a whole-mesh inversion.

#### Scenario: The chrome sphere
Given chrome_test
When the frame is dumped
Then the sphere's visible positions are its near hemisphere's and its normals face the camera, and its
specular matches F x the PMREM along the reflected ray (median relative error at most 0.2).

### Requirement: Only the repaired meshes change
The repair SHALL change only the repaired meshes' triangles: every other mesh and every vertex SHALL
be untouched.

#### Scenario: Sponza
Given Sponza Camera C
When the frame is compared with the build before the repair
Then the G-buffer positions are identical outside the repaired meshes' screen bounds.
