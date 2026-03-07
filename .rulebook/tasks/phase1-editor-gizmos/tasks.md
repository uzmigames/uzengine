## 1. Engine — Gizmo Render Pass (C++)

- [x] 1.1 Add `#ifdef UZ_EDITOR` gizmo render pass in `src/renderer/renderer.cpp` — executed after main scene pass, before JPEG capture
- [x] 1.2 Create gizmo geometry builder: arrow cones + shafts for translate, arc rings for rotate, box handles for scale (< 500 triangles per component)
- [x] 1.3 Implement gizmo depth buffer: gizmos depth-test against each other but not against the scene depth buffer
- [x] 1.4 Implement per-axis color constants: X=`#CC2222`, Y=`#22CC22`, Z=`#2222CC`, center=`#CCCC22`, hover=`#FFFFFF`
- [x] 1.5 Implement gizmo scale with camera distance: `gizmoScale = distance(cameraPos, gizmoOrigin) * 0.1`
- [x] 1.6 Implement hover highlight: when `editor.setGizmoHover` is received, re-color the specified axis to white (via root constant brightness multiplier in `drawMode`)

## 2. Engine — Node Picking (C++)

- [x] 2.1 Implement pick buffer render pass: render scene to a 1/4-resolution buffer where each pixel encodes the node's integer ID as an RGBA value
- [x] 2.2 Implement CPU readback of pick buffer pixel at given screen coordinates
- [x] 2.3 Implement box pick readback: find all unique node IDs within a screen rectangle
- [x] 2.4 Register `editor.pickNode` IPC handler — reads pick buffer at `{screen_x, screen_y}`, returns `{nodeId: N}` or `{nodeId: null}`
- [x] 2.5 Register `editor.boxPick` IPC handler — reads pick buffer within `{x, y, width, height}`, returns `{nodeIds: [...]}`

## 3. Engine — Transform IPC Handlers (C++)

- [x] 3.1 Register `editor.setGizmo` — stores active node ID and gizmo mode; triggers gizmo redraw
- [x] 3.2 Register `editor.setGizmoHover` — stores hovered axis; updates gizmo color on next frame
- [x] 3.3 Register `editor.setGizmoSpace` — toggles local vs world coordinate frame for gizmo rendering
- [x] 3.4 Register `editor.setNodeTransformLive` — applies position/rotation/scale to node without creating undo entry; triggers re-render
- [x] 3.5 Register `editor.commitTransform` — marks transform as final; returns authoritative `{transform: {...}}` to extension
- [x] 3.6 Register `editor.getCamera` — returns `{view: [16], proj: [16], position: [3]}` for the editor camera

## 4. TypeScript — GizmoController

- [x] 4.1 Create `src/editor/gizmoController.ts` — class that manages gizmo state: `mode`, `constraint`, `space`, `isDragging`
- [x] 4.2 Implement `screenToWorldRay(px, py, width, height, viewMatrix, projMatrix): Ray` using inverse VP matrix
- [x] 4.3 Implement `hitTestAxis(ray, gizmoOrigin, axisDir, hitRadius): boolean` using nearest-point-between-two-lines
- [x] 4.4 Implement `hitTestPlane(ray, planeOrigin, planeNormal): Vec3 | null` using ray-plane intersection
- [x] 4.5 Implement translate drag: project ray onto constrained axis/plane; compute world-space delta; send `editor.setNodeTransformLive`
- [x] 4.6 Implement rotate drag: compute signed angle around axis from mouse position on rotation plane; send `editor.setNodeTransformLive`
- [x] 4.7 Implement scale drag: accumulate signed drag distance along screen-projected axis; send `editor.setNodeTransformLive`
- [x] 4.8 Implement grid snap: `Math.round(value / gridSize) * gridSize`; read `gridSize` from project config; activate when `Ctrl` is held

## 5. TypeScript — Viewport Canvas Events

- [x] 5.1 Wire `mousedown` on canvas: hit-test gizmo axes first (TS-side using cached camera matrices); if no hit, call `editor.pickNode` for node selection
- [x] 5.2 Wire `mousemove` on canvas: if dragging, compute and send `editor.setNodeTransformLive` (throttled to 60 calls/sec via `requestAnimationFrame` loop)
- [x] 5.3 Wire `mouseup` on canvas: end drag, call `editor.commitTransform`, push `TransformCommand` to `UndoStack`
- [x] 5.4 Wire `mousemove` (no drag): call `hitTestAxis` for hover (TS-side); send `editor.setGizmoHover` with result
- [x] 5.5 Handle `Ctrl+mousedown` drag on empty canvas area as box-select start: draw rubber-band rectangle, on mouseup call `editor.boxPick`
- [x] 5.6 Handle axis constraint hotkeys during drag: `X`, `Y`, `Z` keys switch constraint mid-drag without restarting
- [x] 5.7 Fetch and cache camera matrices every 200ms via `editor.getCamera`; refresh cache on viewport resize

## 6. TypeScript — Toolbar Integration

- [x] 6.1 Add gizmo mode buttons to viewport webview toolbar: Move (W), Rotate (E), Scale (R); clicking sends `editor.setGizmo`
- [x] 6.2 Add Local/World toggle button; clicking sends `editor.setGizmoSpace`
- [x] 6.3 Show snap indicator in toolbar: `[Snap: 0.25m]` when `Ctrl` is held during drag
- [x] 6.4 Register VSCode keybindings `W`, `E`, `R` with `when: uzengine.viewportFocused` to switch gizmo mode
- [x] 6.5 Register `F` key (frame selection): call `editor.focusNode({nodeId})` IPC to center camera on selection

## 7. Multi-Select Gizmo

- [x] 7.1 When `SelectionManager.count > 1`, compute centroid of all selected node world positions
- [x] 7.2 Send `editor.setGizmo` with `{nodeIds: [...], pivot: [cx, cy, cz]}` to position gizmo at centroid
- [x] 7.3 On translate drag, apply the same world-space delta to each selected node via individual `editor.setNodeTransformLive` calls
- [x] 7.4 On mouseup, push a single `BulkPropertyCommand` covering all moved nodes to `UndoStack`

## 8. Testing (TypeScript)

- [x] 8.1 Unit test `screenToWorldRay` — known camera setup, verify ray direction matches expected world vector
- [x] 8.2 Unit test `hitTestAxis` — parallel ray (should not hit), perpendicular near ray (should hit), far ray (should not hit)
- [x] 8.3 Unit test translate drag — mock IPC, verify `setNodeTransformLive` called with correct position delta
- [x] 8.4 Unit test grid snap — value 0.37 with gridSize 0.25 snaps to 0.25; value 0.38 snaps to 0.5
- [x] 8.5 Unit test rotate drag — 90-degree drag around Y produces rotation delta of π/2
- [x] 8.6 Unit test `TransformCommand` — execute and undo call IPC with correct old/new transforms
- [x] 8.7 Integration test: mousedown + mousemove + mouseup sequence creates exactly one undo entry
- [x] 8.8 Test `editor.pickNode` returns `null` when clicking empty space; returns node ID when clicking object

## 9. Documentation

- [x] 9.1 Verify `docs/specs/editor-gizmos.md` matches implementation
- [x] 9.2 Document all new `editor.*` IPC handlers in `docs/guides/ipc-mcp.md`
- [x] 9.3 Add gizmo usage (W/E/R, Ctrl snap, Local/World) to `docs/guides/getting-started.md`
