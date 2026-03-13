## 1. Implementation

- [ ] 1.1 Implement shadow atlas allocation — single 4096×4096 D16_UNORM atlas. Tile allocator assigns rectangular regions per light: point=6×512² tiles (3072×1024), spot=1×1024² tile. Max ~16 spot or ~4 point lights in atlas.
- [ ] 1.2 Implement spot light shadow rendering — for each shadowed spot: build perspective projection from spot position, direction, outerConeAngle. Render scene depth to atlas tile. Vertex shader: `clipPos = spotViewProj * worldPos`.
- [ ] 1.3 Implement point light cubemap shadow rendering — for each shadowed point: render 6 faces using standard cubemap view matrices (+X, -X, +Y, -Y, +Z, +Z). Each face: perspective projection with 90° FOV, near=0.1, far=light range. Render depth to 6 atlas tiles.
- [ ] 1.4 Implement spot shadow sampling in lighting pass — in deferred_lighting_ps: for shadowed spot lights, project world position through spotViewProj → atlas UV. Sample depth, compare, apply PCF (4-tap Poisson). Multiply light contribution by shadow factor.
- [ ] 1.5 Implement point shadow sampling in lighting pass — compute light-to-pixel direction, select cubemap face (dominant axis), project to face UV → atlas tile UV. Sample depth (linearized), compare distance, PCF. Handle face seam filtering.
- [ ] 1.6 Implement shadow importance sorting — each frame: rank shadowed lights by `screenArea * intensity / distance²`. Highest-importance lights get shadow atlas tiles first. Lower-priority lights are unshadowed.
- [ ] 1.7 Add shadow parameters to LightGPU struct — `bCastShadows`, `shadowBias` (0.5 default), `shadowResolutionScale` (1.0), `shadowAtlasUV` (rect in atlas for sampling).
- [ ] 1.8 Implement per-light shadow CB — `ShadowAtlasCB`: array of viewProj matrices + atlas UV rects for up to 16 shadowed local lights. Bound at register b5 or next available slot.
- [ ] 1.9 Add shadow pass variants to render graph — for each shadowed local light: add depth-only render pass targeting atlas tile. Execute before main lighting pass.
- [ ] 1.10 Create point/spot shadow demo — room with 2 point lights and 1 spot light. Objects casting shadows on walls/floor from all light sources. Toggle shadows per light.

## 2. Visual Verification

- [ ] 2.1 Screenshot: room with shadowed point light — objects cast shadows on all walls
- [ ] 2.2 Screenshot: spot light cone shadow — objects in cone are shadowed
- [ ] 2.3 Screenshot: multiple shadowed lights — no shadow atlas conflicts
- [ ] 2.4 Screenshot: shadows OFF vs ON — dramatic difference in believability
- [ ] 2.5 Screenshot: shadow atlas debug view — tile allocation visible
- [ ] 2.6 Regression: existing directional shadow (CSM) still works

## 3. Documentation

- [ ] 3.1 Document shadow atlas layout and allocation strategy
- [ ] 3.2 Add point/spot shadow sampling algorithm with UE5 reference
- [ ] 3.3 Document shadow importance ranking and performance budget
