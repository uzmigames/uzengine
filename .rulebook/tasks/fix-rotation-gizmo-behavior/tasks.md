## 1. Replace Rotation Drag with Screen-Space Linear Delta (TypeScript)

- [x] 1.1 In `viewport-panel.ts` `computeDragStart()`, for rotation mode: store the initial screen-space mouse position (px, py) instead of computing a world-space hit point on the rotation plane
- [x] 1.2 In the `mousemove` drag handler (rotation branch), replace the arc-angle computation with screen-space horizontal delta: `deltaAngle = (currentMouseX - dragStartMouseX) * sensitivity` where sensitivity = `Math.PI / 360` (~0.5 deg/pixel)
- [x] 1.3 Apply cumulative rotation from drag start: `newRotation = quatMul(quatFromAxisAngle(worldAxis, deltaAngle), dragStartRotation)` — no more vector normalization, cross products, or acos
- [x] 1.4 Add Ctrl-snap support for rotation: when Ctrl is held, snap `deltaAngle` to 15-degree increments (`snapVal(deltaAngle, Math.PI / 12)`)

## 2. Lock Rotation Gizmo to World-Space Axes (TypeScript)

- [x] 2.1 Verify that `getAxisDir()` always returns world-space directions `{1,0,0}`, `{0,1,0}`, `{0,0,1}` for rotation mode (already the case — confirmed)
- [x] 2.2 Verify that the C++ gizmo render (`gizmo_transform.cpp` / `editor_component.cpp`) does NOT apply the node's rotation to the gizmo orientation — gizmo rings stay aligned to world axes (confirmed: XMMatrixTranslation only, no node rotation applied)

## 3. Multi-Select Rotation Support (TypeScript)

- [x] 3.1 In the rotation drag branch, when `selectedNodeIds.length >= 2`, apply the same `deltaAngle` rotation around the world axis to each node's stored `dragStartTransform.rotation` individually via `editor.setNodeTransformLive`
- [x] 3.2 On mouseup for multi-select rotation, push a bulk undo entry (same pattern as translate multi-select in task 7.4 of phase1-editor-gizmos) — already handled by existing mouseup code

## 4. Testing (TypeScript)

- [x] 4.1 Unit test: rotation drag right (+X screen delta) produces positive angle around the constrained axis
- [x] 4.2 Unit test: rotation drag left (-X screen delta) produces negative angle around the constrained axis
- [x] 4.3 Unit test: Ctrl-snap rounds to 15-degree increments
- [x] 4.4 Unit test: rotation drag with multi-select applies same delta to all nodes
- [x] 4.5 Verify all existing gizmo tests still pass (no regressions) <!-- 776/776 pass -->

## 5. Documentation

- [x] 5.1 Update `docs/specs/editor/editor-gizmos.md` rotation drag section to describe the screen-space linear behavior
- [x] 5.2 Update `docs/guides/getting-started.md` if rotation usage instructions changed — no changes needed, existing docs are consistent
