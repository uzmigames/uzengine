## 1. Replace Linear Slider with Arc-Sweep Rotation (TypeScript)

- [x] 1.1 In `viewport-panel.ts`, add a helper `worldToScreen(worldPos)` that projects a world-space point to screen-space (px, py) using the cached camera view/proj matrices
- [x] 1.2 In `viewport-panel.ts`, add state variables: `dragStartAngle` (atan2 angle at drag start), `dragGizmoScreenPos` (projected gizmo center {sx, sy})
- [x] 1.3 In `computeDragStart()` for rotation mode: project gizmo origin to screen space via `worldToScreen`; compute `dragStartAngle = atan2(mouseY - gizmoScreenY, mouseX - gizmoScreenX)`; store both values
- [x] 1.4 In the `mousemove` rotation drag branch: compute `currentAngle = atan2(mouseY - gizmoScreenY, mouseX - gizmoScreenX)`; compute `rawDelta = currentAngle - dragStartAngle`; normalize to [-PI, PI] range
- [x] 1.5 Determine sign flip: compute `dot(worldAxis, cameraForward)`; if negative, negate the delta (ensures clockwise screen sweep = positive rotation when looking along the axis)
- [x] 1.6 Apply cumulative rotation: `newRotation = quatMul(quatFromAxisAngle(worldAxis, deltaAngle), dragStartRotation)` — same quaternion math as before, just with arc-sweep delta
- [x] 1.7 Ctrl-snap: when Ctrl is held, snap `deltaAngle` to 15-degree increments (`snapVal(deltaAngle, Math.PI / 12)`) — no change in snap logic
- [x] 1.8 Remove the `dragStartScreenX` variable and all linear slider references

## 2. Multi-Select Arc Rotation (TypeScript)

- [x] 2.1 In the rotation drag branch, when `selectedNodeIds.length >= 2`, apply the same arc-sweep `deltaAngle` to each node's stored `dragStartTransform.rotation` individually
- [x] 2.2 Verify mouseup bulk undo entry still works correctly with the new delta computation

## 3. Testing (TypeScript)

- [x] 3.1 Unit test: clockwise screen sweep (positive atan2 delta) produces positive rotation around the axis when looking along it <!-- 777/777 pass -->
- [x] 3.2 Unit test: counter-clockwise screen sweep produces negative rotation
- [x] 3.3 Unit test: sign flip when axis points away from camera (dot < 0)
- [x] 3.4 Unit test: Ctrl-snap rounds arc-sweep delta to 15-degree increments
- [x] 3.5 Unit test: multi-select applies same arc-sweep delta to all nodes
- [x] 3.6 Unit test: `worldToScreen` projects correctly with known camera matrices
- [x] 3.7 Verify all existing gizmo tests still pass (no regressions) <!-- 777/777 pass -->

## 4. Documentation

- [x] 4.1 Update `docs/specs/editor/editor-gizmos.md` rotation drag section to describe the arc-sweep behavior
- [x] 4.2 Update `docs/guides/getting-started.md` if rotation usage instructions changed — no changes needed, existing docs are consistent
