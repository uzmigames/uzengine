# Renderer Selection System

## ADDED Requirements

### Requirement: GPU Pick Buffer
The renderer MUST render all scene proxies into a 1/4-resolution RGBA8 render target with each proxy's node_id encoded as RGB bytes (R=bits[0:7], G=bits[8:15], B=bits[16:23]).

#### Scenario: Pick buffer renders node IDs
- GIVEN a scene with 3 mesh proxies (node_id=1, node_id=2, node_id=3)
- WHEN the pick_draw render pass executes
- THEN each pixel in the pick buffer contains the RGB-encoded node_id of the frontmost mesh at that pixel
- AND pixels with no mesh coverage contain (0,0,0) meaning nodeId=0

#### Scenario: Pick buffer respects depth
- GIVEN two overlapping meshes where mesh A (node_id=1) is in front of mesh B (node_id=2)
- WHEN the pick_draw render pass executes with depth testing enabled
- THEN pixels in the overlap region contain node_id=1 (the frontmost mesh)

#### Scenario: Pick buffer readback to CPU
- GIVEN a completed pick_draw render pass
- WHEN performReadback() executes
- THEN pickData_ is populated with the RGBA pixel data
- AND pickW_ and pickH_ reflect the 1/4-resolution dimensions

### Requirement: Pick Node Priority Layers
The editor.pickNode handler MUST use a layered picking strategy: GPU pick buffer first, origin-distance fallback second.

#### Scenario: GPU pick hits a mesh
- GIVEN a click at screen coordinates (sx, sy) over a visible mesh with node_id=5
- WHEN editor.pickNode is processed
- THEN the GPU pick buffer returns node_id=5
- AND the origin-distance fallback is NOT executed

#### Scenario: GPU pick misses, origin fallback hits
- GIVEN a click at screen coordinates near a non-mesh node's origin (e.g., a light)
- WHEN editor.pickNode is processed
- THEN the GPU pick buffer returns 0
- AND the origin-distance fallback finds the nearest node within 50px

#### Scenario: Both miss — empty space deselect
- GIVEN a click on empty viewport space with no mesh and no nearby origins
- WHEN editor.pickNode is processed
- THEN pickResult returns nodeId=null
- AND selectedNodeId is set to 0 (deselected)

## MODIFIED Requirements

### Requirement: Selection Outline PSO
The outline PSO MUST use front-face culling and disable depth testing for Blender-style always-visible silhouette outline.

#### Scenario: Outline visible behind occluding geometry
- GIVEN a selected object partially behind another object
- WHEN the outline pass renders with depth_enable=false and cull_mode=CULL_FRONT
- THEN the outline fringe is visible around the entire selected object silhouette
- AND only back-face fringe pixels pass the stencil test (stencil != 1)
