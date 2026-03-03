## 1. CWorldSpaceUI ECS Component

- [ ] 1.1 Add `CWorldSpaceUI` struct to `src/scene/components.h` with fields: `html_path` (std::string), `width_m` (float), `height_m` (float), `offset` (Vec3), `face_camera` (bool, default true), `depth_test` (bool, default true), `scale_by_distance` (bool, default false)
- [ ] 1.2 Register `CWorldSpaceUI` with the ECS component registry so it serializes to scene YAML alongside other components

## 2. Offscreen Render Target per Billboard

- [ ] 2.1 Create `src/ui/world_space_ui.h` declaring `WorldSpaceUISystem` with method `update(entity_id, CWorldSpaceUI&)` called each frame
- [ ] 2.2 Create `src/ui/world_space_ui.cpp` implementing per-entity offscreen render target allocation: one `D3D12Resource` render target per `CWorldSpaceUI` entity, created on component-add and destroyed on component-remove
- [ ] 2.3 Render each `html_path` HTML document into its offscreen render target via UIPass each frame, reusing the existing UIPass HTML rendering pipeline
- [ ] 2.4 Cache the render target per entity; do not re-allocate on each frame

## 3. Billboard Quad Rendering

- [ ] 3.1 Create `src/renderer/billboard_renderer.h` and `src/renderer/billboard_renderer.cpp` implementing `BillboardRenderer::draw(entity_id, world_pos, width_m, height_m, tex_handle, depth_test, scale_by_distance)`
- [ ] 3.2 When `face_camera == true`, compute the billboard rotation matrix as `look_at(world_pos, camera_pos)` projected to the XZ plane (Y-axis-aligned billboard)
- [ ] 3.3 When `scale_by_distance == false`, the quad is physically `width_m x height_m` world meters; apparent size shrinks with distance
- [ ] 3.4 When `scale_by_distance == true`, scale the quad's world-space size by `distance_to_camera / reference_distance` so screen-space size remains constant
- [ ] 3.5 Apply the offscreen render target texture as the quad's albedo
- [ ] 3.6 When `depth_test == true`, submit the billboard quad to the DX12 pipeline with depth test and depth write enabled, participating in the scene depth buffer
- [ ] 3.7 When `depth_test == false`, submit with depth test and write disabled (always-on-top rendering)

## 4. Lua Bridge Integration

- [ ] 4.1 Add `ctx.ui:attach_to_entity(entity_id, html_path, width_m, height_m)` Lua binding in `src/lua/lua_ui_api.cpp` that adds a `CWorldSpaceUI` component to the entity
- [ ] 4.2 Add `ctx.ui:detach_from_entity(entity_id)` Lua binding that removes the `CWorldSpaceUI` component and destroys the offscreen render target
- [ ] 4.3 Add `ctx.ui:update_billboard(entity_id, html_content)` Lua binding that injects new HTML content into the entity's billboard HTML document via `innerHTML` replacement
- [ ] 4.4 Ensure `ctx.ui:set_content(element_id, value)` works identically inside billboard HTML documents as in screen-space UI

## 5. Editor Gizmo

- [ ] 5.1 In `src/editor/gizmos.cpp`, when a `CWorldSpaceUI` entity is selected in the editor, draw a yellow wireframe rectangle at `entity_world_pos + offset` with size `width_m x height_m` in the editor pass
- [ ] 5.2 Orient the gizmo rectangle to face the editor camera (same billboard rotation as the runtime renderer)

## 6. Testing

- [ ] 6.1 Create `tests/ui/test_world_space_billboards.cpp` testing that adding `CWorldSpaceUI` to an entity allocates a non-null offscreen render target
- [ ] 6.2 Test removing `CWorldSpaceUI` destroys the render target (resource reference count drops to zero)
- [ ] 6.3 Test billboard world matrix with `face_camera=true`: verify the quad normal is aligned with the camera-to-quad direction
- [ ] 6.4 Test `scale_by_distance=false`: the quad world size is exactly `width_m x height_m` regardless of camera distance
- [ ] 6.5 Test `scale_by_distance=true`: at 2x reference distance, the quad world size is 2x larger, maintaining constant screen-space size
- [ ] 6.6 Test `depth_test=true` vs `depth_test=false`: verify the DX12 depth state is set correctly for each mode
- [ ] 6.7 Test `ctx.ui:attach_to_entity()` creates a `CWorldSpaceUI` component with the specified `html_path`, `width_m`, and `height_m`
- [ ] 6.8 Test `ctx.ui:detach_from_entity()` removes the component and triggers render target cleanup
- [ ] 6.9 Test `ctx.ui:update_billboard()` injects new content; verify the billboard's HTML document now contains the injected element
- [ ] 6.10 Achieve >= 95% line and branch coverage across `world_space_ui.cpp` and `billboard_renderer.cpp`

## 7. Documentation

- [ ] 7.1 Update `docs/specs/ui-system.md` documenting `CWorldSpaceUI` fields, `face_camera`/`depth_test`/`scale_by_distance` behavior, and Lua API
- [ ] 7.2 Update `docs/specs/renderer.md` documenting the billboard offscreen render target pipeline and depth participation
- [ ] 7.3 Update `docs/specs/editor-gizmos.md` documenting the yellow wireframe gizmo for `CWorldSpaceUI` entities
- [ ] 7.4 Update `docs/specs/lua-scripting.md` documenting `ctx.ui:attach_to_entity()`, `ctx.ui:detach_from_entity()`, and `ctx.ui:update_billboard()`
