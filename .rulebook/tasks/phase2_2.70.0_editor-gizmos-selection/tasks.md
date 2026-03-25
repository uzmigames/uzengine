## 1. Editor Mode Foundation
- [x] 1.1 Wire `--editor` CLI flag — already wired (config.GetBool("editor") → SetEditorMode). Verified editor grid renders with screenshot.
- [x] 1.2 Create `runtime/editor/input/editor_input_handler.h/.cpp` — routes mouse/keyboard to selection or gizmo. Mode state: Translate(W), Rotate(E), Scale(R). Space toggle: World/Local. GizmoMode/GizmoSpace enums, W/E/R hotkeys, Space toggle, left-click delegates to SelectionManager.
- [x] 1.3 Integrate EditorInputHandler into uzengine.cpp render loop (before ScriptSystem::Update, only when editorMode=true). Added includes, created SelectionManager + EditorInputHandler after SceneRenderer init, wired Update(dt) before Lua scripts, syncs selected entity to SceneRenderer each frame.

## 2. Selection System
- [x] 2.1 Create `runtime/editor/selection/selection_manager.h/.cpp` — stores selected Entity, provides Select(entity), Deselect(), GetSelected(). Also HasSelection(), SelectAtScreenPos() with viewport dims, camera, world, renderer params.
- [x] 2.2 Implement mouse ray construction from screen coordinates: UnprojectPoint() helper converts NDC→world via inverse VP. Screen→NDC with Y-flip, reversed-Z (near=1, far=0). Ray from camera position along (farPt - nearPt).Normalized().
- [x] 2.3 Implement AABB hit testing: RayIntersectsAABB() slab method (Kay & Kajiya 1986). Iterates all SceneComponent+StaticMeshComponent entities, gets mesh bounds from SceneRenderer::GetMeshBounds(), applies scale+translation, selects nearest tMin hit.
- [x] 2.4 Wire left-click in EditorInputHandler → SelectionManager::SelectAtScreenPos(mouseX, mouseY, vw, vh, camera, world, renderer). Guards against RMB-held (camera orbit) interference.
- [x] 2.5 Add `GetSelectedEntity()` / `SetSelectedEntity()` / `GetMeshBounds()` to SceneRenderer. Entity stored as m_selectedEntity in scene_renderer_impl.h. Bounds accessor reads m_meshBoundsMin/Max vectors. Implemented in scene_renderer_init.cpp.

## 3. Selection Outline
- [ ] 3.1 Create `runtime/editor/outline/outline_pass.h/.cpp` — stencil-based outline renderer.
- [ ] 3.2 Create `runtime/shaders/editor/outline_stencil_vs.hlsl` — re-renders selected object mesh to stencil buffer (depth test, no color write).
- [ ] 3.3 Create `runtime/shaders/editor/outline_detect_ps.hlsl` — fullscreen pass reads stencil, detects edges via 3x3 kernel, outputs colored outline.
- [ ] 3.4 Create outline pipeline in PipelineLibrary: stencil-write pass + fullscreen outline detect pass.
- [ ] 3.5 Integrate outline pass into `frame_overlay.cpp` — runs after GBuffer, before editor grid. Only when editorMode && selectedEntity valid.
- [ ] 3.6 Outline color: orange (#FF8C00) default, configurable via CVar `r.Editor.OutlineColor`.

## 4. Gizmo Rendering
- [ ] 4.1 Create `runtime/editor/gizmo/gizmo_renderer.h/.cpp` — draws translate/rotate/scale gizmo at selected entity's world position.
- [ ] 4.2 Create `runtime/shaders/editor/gizmo_vs.hlsl` + `gizmo_ps.hlsl` — unlit colored lines/triangles, depth test against scene but always-on-top option.
- [ ] 4.3 Implement translate gizmo geometry: 3 axis arrows (RGB = XYZ) + 3 plane handles (YZ=red, XZ=green, XY=blue) + center screen-space handle.
- [ ] 4.4 Implement rotate gizmo geometry: 3 axis circles (RGB = XYZ) + trackball (screen-space rotation).
- [ ] 4.5 Implement scale gizmo geometry: 3 axis lines with cubes at ends (RGB = XYZ) + center uniform scale cube.
- [ ] 4.6 Gizmo size: constant screen-space size (scales with camera distance). UE5 default ~120px.
- [ ] 4.7 Create gizmo pipeline in PipelineLibrary: line + triangle pipelines with depth test + alpha blend.
- [ ] 4.8 Integrate gizmo draw into `frame_overlay.cpp` after editor grid, before stats overlay.

## 5. Gizmo Interaction
- [ ] 5.1 Implement gizmo hit testing: ray vs axis line (cylinder test), ray vs plane handle (quad test), ray vs circle (torus test). Returns axis/plane/none.
- [ ] 5.2 Implement translate drag: project mouse delta onto gizmo axis/plane, compute world-space delta position. Apply to SceneComponent::SetPosition.
- [ ] 5.3 Implement rotate drag: mouse delta → angle around selected axis. Use arcball for trackball mode. Apply to SceneComponent::SetRotation via quaternion.
- [ ] 5.4 Implement scale drag: mouse delta along axis → scale factor. Uniform scale via center handle. Apply to SceneComponent::SetScale.
- [ ] 5.5 Implement snapping: translate snap to grid (default 10cm), rotate snap to degrees (default 15°), scale snap to increments (default 0.25). Toggle with Ctrl held.
- [ ] 5.6 Gizmo highlight: hovered axis turns yellow. Active (dragging) axis turns white.
- [ ] 5.7 World vs Local space: W key cycles mode. World = gizmo aligned to world axes. Local = gizmo aligned to entity rotation.

## 6. Lua API
- [ ] 6.1 Add `uz.editor.get_selected()` → entity or nil.
- [ ] 6.2 Add `uz.editor.select(entity)` / `uz.editor.deselect()`.
- [ ] 6.3 Add `uz.editor.set_gizmo_mode(mode)` — "translate"/"rotate"/"scale".
- [ ] 6.4 Add `uz.editor.set_gizmo_space(space)` — "world"/"local".

## 7. Testing
- [ ] 7.1 Test: click on entity selects it (SelectionManager returns correct entity).
- [ ] 7.2 Test: click on empty space deselects.
- [ ] 7.3 Test: outline renders around selected entity (screenshot verification).
- [ ] 7.4 Test: translate gizmo moves entity along X axis (verify position changed in SceneComponent).
- [ ] 7.5 Test: rotate gizmo rotates entity around Y axis (verify rotation quaternion changed).
- [ ] 7.6 Test: scale gizmo scales entity uniformly (verify scale changed).
- [ ] 7.7 Test: W/E/R keys switch gizmo mode.
- [ ] 7.8 Test: gizmo not visible when editorMode=false.
- [ ] 7.9 Test: snap translation to 10cm grid when Ctrl held.
- [ ] 7.10 Visual test: gizmo matches UE5 style (RGB axes, correct proportions). Screenshot comparison.
- [ ] 7.11 Verify 95%+ coverage on all new code.

## 8. Documentation
- [ ] 8.1 Update `docs/specs/editor-gizmos.md` with gizmo interaction spec.
- [ ] 8.2 Update `docs/guides/editor-workflow.md` with gizmo controls reference.
- [ ] 8.3 Update `docs/shaders.md` with new editor shaders (outline, gizmo).
- [ ] 8.4 Update `sample/README.md` with `--editor` flag documentation.
