## 0. Engine Systems (COMPLETE — DO NOT REWRITE)

> **IMPORTANT**: All core systems below are FULLY IMPLEMENTED and production-ready.
> Do NOT create new classes, shaders, or systems from scratch.
> ALL tasks below are INTEGRATION ONLY — wire existing APIs into the VFX viewport demo.

- [x] 0.1 GizmoRenderer — `editor/gizmo/gizmo_renderer.h/.cpp` (pre-baked geometry, constant screen-space sizing, dual pipelines)
- [x] 0.2 GizmoInteraction — `editor/gizmo/gizmo_interaction.h/.cpp` (hit testing, drag state, transform deltas, snapping)
- [x] 0.3 OutlinePass — `editor/outline/outline_pass.h/.cpp` (two-pass mask+detect, R8Unorm mask, 3x3 edge detection, alpha-blended outline)
- [x] 0.4 SelectionManager — `editor/selection/selection_manager.h/.cpp` (ray-AABB slab method, reversed-Z unprojection)
- [x] 0.5 Shaders — `gizmo_vs.hlsl`, `gizmo_ps.hlsl`, `outline_mask_ps.hlsl`, `outline_detect_ps.hlsl`, `fullscreen_vs.hlsl` (all in EngineShaders enum)
- [x] 0.6 Enums — `GizmoMode` (Translate/Rotate/Scale), `GizmoSpace` (World/Local), `GizmoHitResult` in `editor/input/editor_input_handler.h`

## 1. VFX Viewport Demo Integration (WIRING ONLY — use existing APIs)

### 1.1 Selection System Wiring
- [x] 1.1.1 Add `#include` for existing SelectionManager, GizmoRenderer, GizmoInteraction, OutlinePass, GizmoMode/GizmoSpace to `vfx_viewport_demo.h`. Added includes for outline_pass.h, gizmo_renderer.h, gizmo_interaction.h, editor_input_handler.h, quaternion.h, vector3.h.
- [x] 1.1.2 Add member variables to VFXViewportDemo: `uint32_t m_selectedMeshIndex = UINT32_MAX` (which of the 5 meshes is selected). Added MeshInstance struct and m_selectedMeshIndex.
- [x] 1.1.3 Wire click-to-select in HandleInput(): on LMB click (without Alt held), construct pick ray via Camera inverse VP, test against bounding sphere of current mesh, set m_selectedMeshIndex. Uses reversed-Z unprojection (near=1, far=0).
- [x] 1.1.4 Wire deselect on Escape key or clicking empty space. Escape clears selection and ends gizmo drag.

### 1.2 Outline Pass Wiring
- [x] 1.2.1 Add `OutlinePass m_outlinePass` member and `bool m_outlineEnabled = true` toggle.
- [x] 1.2.2 Call existing `m_outlinePass.Init()` in OnInit() with RGBA16Float RTV format and D32Float DSV format.
- [x] 1.2.3 Call existing `m_outlinePass.Resize()` in OnResize().
- [x] 1.2.4 Call existing `m_outlinePass.Shutdown()` in OnShutdown().
- [x] 1.2.5 Call existing `m_outlinePass.Draw()` after distortion pass, before tonemap. Builds temporary DrawCommand/GPUMesh vectors from ProceduralMesh to match OutlinePass API.
- [x] 1.2.6 Add O key toggle, update help text with O key and all gizmo shortcuts.

### 1.3 Gizmo Renderer Wiring
- [x] 1.3.1 Add `GizmoRenderer m_gizmoRenderer` and `GizmoInteraction m_gizmoInteraction` members.
- [x] 1.3.2 Add `GizmoMode m_gizmoMode` and `GizmoSpace m_gizmoSpace` state.
- [x] 1.3.3 Call existing `m_gizmoRenderer.Init()` in OnInit() with RGBA16Float + D32Float.
- [x] 1.3.4 Call existing `m_gizmoRenderer.Shutdown()` in OnShutdown().
- [x] 1.3.5 Wire W/E/R keyboard shortcuts for mode switching (only when selected). R key falls back to env rotation when nothing selected. Updated help text.
- [x] 1.3.6 Call existing `m_gizmoRenderer.Draw()` in RenderGizmoPass() when a mesh is selected. Begins HDR+depth render pass, computes hover/active axis from GizmoInteraction.

### 1.4 Gizmo Interaction Wiring
- [x] 1.4.1 Call existing `m_gizmoInteraction.HitTest()` every frame when selected, update hover via SetHoveredAxis(). Computes screen scale from camera distance and FOV.
- [x] 1.4.2 Call existing `m_gizmoInteraction.BeginDrag()` on LMB press over gizmo handle (when not Alt-held).
- [x] 1.4.3 Call existing `m_gizmoInteraction.UpdateDrag()` on LMB held, apply returned deltas to m_meshTransforms. Absolute position/rotation/scale from drag start + delta.
- [x] 1.4.4 Call existing `m_gizmoInteraction.EndDrag()` on LMB release (IsMouseJustReleased).
- [x] 1.4.5 Wire Ctrl modifier for snap-to-grid using kDefaultTranslateSnap (10cm), kDefaultRotateSnap (15deg), kDefaultScaleSnap (0.25).
- [x] 1.4.6 Ensure per-mesh transforms are mutable so gizmo can modify them. m_meshTransforms array is non-const, gizmo drag modifies it directly.

### 1.5 Per-Mesh Transform State (refactor hardcoded transforms)
- [x] 1.5.1 Add `MeshInstance` struct and `std::array<MeshInstance, kMeshCount> m_meshTransforms`. MeshInstance has position, rotation (Quat), scale.
- [x] 1.5.2 Initialize with current hardcoded values from RenderScenePass/RenderShadowPass. Ground plane (index 2) gets special transform: rotX(pi/2), translate(0,0,-0.1), scale(20,1,20).
- [x] 1.5.3 Refactor RenderScenePass to read from m_meshTransforms via BuildMeshWorldMatrix(). Manual Alt+LMB rotation is composed on top.
- [x] 1.5.4 Refactor RenderShadowPass to read from m_meshTransforms via BuildMeshWorldMatrix().
- [x] 1.5.5 Gizmo drag deltas modify m_meshTransforms[m_selectedMeshIndex]. Translate sets absolute position, rotate applies delta*startRot, scale adds delta to startScale.

## 2. Testing

- [ ] 2.1 Visual test: click on sphere → orange outline appears around it
- [ ] 2.2 Visual test: click on ground plane → outline on ground plane
- [ ] 2.3 Visual test: W key → translate gizmo arrows visible at selected mesh
- [ ] 2.4 Visual test: E key → rotate gizmo circles visible
- [ ] 2.5 Visual test: R key → scale gizmo cubes visible
- [ ] 2.6 Visual test: drag X arrow → mesh moves along X axis only
- [ ] 2.7 Visual test: drag rotate ring → mesh rotates around axis
- [ ] 2.8 Visual test: Escape → deselect, gizmo and outline disappear
- [ ] 2.9 Visual test: click empty space → deselect
- [ ] 2.10 Test gizmo constant screen-space size: zoom in/out, gizmo stays ~120px
- [ ] 2.11 Test Ctrl+drag snapping (translate snaps to 1cm grid)
- [ ] 2.12 Screenshot verification: capture with gizmo visible, verify render correctness

## 3. Documentation

- [ ] 3.1 Update `docs/guides/vfx-viewport-controls.md` with outline/gizmo keyboard shortcuts
- [ ] 3.2 Add gizmo interaction guide (W/E/R modes, Ctrl snap, mouse drag behavior)
- [ ] 3.3 Add `docs/engine/22-developer-layer/` editor tools architecture documentation

## 5. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 5.1 Update or create documentation covering the implementation
- [ ] 5.2 Write tests covering the new behavior
- [ ] 5.3 Run tests and confirm they pass
