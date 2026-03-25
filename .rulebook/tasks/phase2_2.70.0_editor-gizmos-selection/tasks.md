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
- [x] 3.1 Created `runtime/editor/outline/outline_pass.h/.cpp` — mask-based outline (R8Unorm mask + edge detect). Two-pass: mask selected mesh → fullscreen edge detect with 3x3 kernel.
- [x] 3.2 Created `runtime/shaders/editor/outline_mask_ps.hlsl` — outputs 1.0 for selected mesh pixels. Uses existing mesh_vs for vertex transform + GBuffer depth for visibility.
- [x] 3.3 Created `runtime/shaders/editor/outline_detect_ps.hlsl` — fullscreen edge detection. Samples mask at 8 neighbors, outputs outline color where center=0 and neighbor>0.5. Configurable thickness.
- [x] 3.4 Created OutlineMaskPipeline + OutlineDetectPipeline in OutlinePass::Init. Mask: R8Unorm RT, depth read, reversed-Z. Detect: backbuffer RT, alpha blend, no depth.
- [x] 3.5 Integrated into frame_overlay.cpp as render graph pass "OutlinePass" before OverlayPass. Guarded by editorMode && selectedEntity.IsValid().
- [x] 3.6 Outline color: orange (1.0, 0.549, 0.0) default. Configurable via SetOutlineColor(). entityId field added to DrawCommand for mesh matching.

## 4. Gizmo Rendering
- [x] 4.1 Create `runtime/editor/gizmo/gizmo_renderer.h/.cpp` — GizmoRenderer class with Init/Shutdown/Draw/IsInitialized. Owns geometry VBs/IBs, line+triangle pipelines, GizmoCB layout matching b1 root slot. GizmoVertex struct (pos[3]+color[3]=24 bytes).
- [x] 4.2 Create `runtime/shaders/editor/gizmo_vs.hlsl` + `gizmo_ps.hlsl` — unlit colored output. VS scales vertices by screenScale + translates to gizmoWorldPos. PS passes through color+alpha. Entry point = main. Auto-embedded via uzembed.
- [x] 4.3 Implement translate gizmo geometry: 3 axis stems (LineList, 2 verts each), 3 arrow cones (8-segment, 16 tris each), 3 plane handles (XY=blue, XZ=green, YZ=red quads at offset 0.3), center cube (gray). All pre-baked with axis colors.
- [x] 4.4 Implement rotate gizmo geometry: 3 axis circles (64-segment LineList rings, radius 0.85). X-circle in YZ plane, Y-circle in XZ, Z-circle in XY. Per-vertex axis colors.
- [x] 4.5 Implement scale gizmo geometry: 3 axis stems (same as translate) + 3 tip cubes (half-size 0.04) + center cube (half-size 0.06, gray). Indexed triangle lists.
- [x] 4.6 Constant screen-space size: screenScale = dist * tan(fov/2) * (120px / 720). Computed per-frame in Draw(), uploaded via GizmoCB. kTargetScreenPx=120.
- [x] 4.7 Two pipelines: GizmoLinePipeline (LineList) + GizmoTrianglePipeline (TriangleList). Both: depth read (GreaterEqual reversed-Z), no depth write, alpha blend (srcAlpha/invSrcAlpha), no backface culling. Bindless: no IA input layout — vertices fetched via SV_VertexID from StructuredBuffer with SRV index in GizmoCB.vertexBufferSrv. SM5 fallback: register(t0) SRV.
- [x] 4.8 Integrated into frame_overlay.cpp OverlayPass after editor grid, before stats overlay. Entity position extracted from DrawList worldMatrix via getTranslation(). GizmoMode/Space flow: EditorInputHandler -> uzengine.cpp -> SceneRenderer (m_gizmoMode/Space uint8_t) -> FrameContext (gizmoModeRaw/SpaceRaw) -> frame_overlay.cpp cast to enum. Hover/active axis highlighting: yellow=hover, white=active, 0.3 alpha for inactive during drag.

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
