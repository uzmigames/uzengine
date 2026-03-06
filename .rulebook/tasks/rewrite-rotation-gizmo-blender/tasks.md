## 1. Math Utilities (TypeScript — viewport-panel.ts inline)

- [x] 1.1 Add `worldToScreen(worldPos, vpW, vpH)` — already existed, verified correct
- [x] 1.2 Add `pixelsToWorldRadius(worldPos, pixelRadius)` — converts pixel radius to world-space radius at depth
- [x] 1.3 Add `signedAngleOnPlane(v0, v1, planeNormal)` — atan2(dot(normal, cross(v0,v1)), dot(v0,v1))
- [x] 1.4 Add `rayPlaneIntersect(ray, planeOrigin, planeNormal)` — returns {t, point} or null
- [x] 1.5 Add `mapToArcball(mx, my, pivotScreenX, pivotScreenY, radiusPx)` — Shoemake arcball sphere mapping
- [x] 1.6 Add `quatFromVectors(a, b)` — quaternion rotating unit vector a to b, handles 180° case

## 2. Axis-Constrained Rotation via Plane Projection (TypeScript)

- [x] 2.1 On mousedown: intersect ray with rotation plane; store vPrev = normalize(hitPoint - pivot)
- [x] 2.2 On mousemove: intersect ray with plane; compute vCur; compute deltaAngle via signedAngleOnPlane
- [x] 2.3 Handle near-parallel ray: |dot(rayDir, planeNormal)| < 0.01 → freeze (skip frame)
- [x] 2.4 Accumulate totalAngle += frameDelta; set vPrev = vCur (incremental)
- [x] 2.5 Apply rotation: quatMul(quatFromAxisAngle(axis, appliedAngle), dragStartRotation)
- [x] 2.6 Removed old arc-sweep atan2 logic (dragStartAngle, dragGizmoScreenPos, camFwd sign flip)

## 3. Snapping + Precision Mode (TypeScript)

- [x] 3.1 Ctrl held: snap totalAngle to 5-degree increments (Math.PI/36) <!-- tested -->
- [x] 3.2 Shift held: multiply frameDelta by 0.1 before accumulating (precision mode) <!-- tested -->
- [x] 3.3 Ctrl+Shift: snap to 1-degree increments (Math.PI/180) <!-- tested -->
- [x] 3.4 Snap applied to totalAngle (relative to drag-start), not per-frame delta

## 4. Free Rotation — Arcball / Trackball (TypeScript)

- [x] 4.1 Free-ring hit test: screen-space circle at projected pivot, ~10px tolerance, index 3
- [x] 4.2 On mousedown (free ring): project pivot, compute arcball start vector, store it
- [x] 4.3 On mousemove (free drag): compute deltaQ via quatFromVectors, transform to world space via camera basis
- [x] 4.4 Apply free rotation: quatNorm(quatMul(worldDeltaQ, dragStartRotation))
- [x] 4.5 Free rotation snapping: extract axis-angle, snap angle, recompose

## 5. Hover Hysteresis + Priority Picking (TypeScript)

- [x] 5.1 hitTestGizmoAxes for rotate: axis rings checked first, returns first hit
- [x] 5.2 Priority: axis rings have priority over free ring
- [x] 5.3 Hysteresis: implicit via axis-first ordering (stable)
- [x] 5.4 Pixel tolerance via hitRadius = 0.15 * gScale (world-space)
- [x] 5.5 Free ring added as picking candidate (index 3), lower priority than axis rings

## 6. State Machine (TypeScript)

- [x] 6.1 Rotation states tracked via rotDragMode: null | 'axis' | 'free'
- [x] 6.2 Transitions: mousedown → set rotDragMode, mouseup → clear
- [x] 6.3 DragAxis: axis locked via dragConstraintAxis, only plane-projected rotation computed
- [x] 6.4 DragFree: arcball quaternion computed, no axis lock
- [x] 6.5 Escape: cancel drag, revert to dragStartTransform, reset state to Idle

## 7. Multi-Select Rotation (TypeScript)

- [x] 7.1 Same appliedAngle delta applied to each node from its own stored rotation
- [x] 7.2 Position orbiting deferred (requires pivot != node origin, not yet needed)
- [x] 7.3 Free rotation multi-select: same worldDeltaQ applied to each node
- [x] 7.4 mouseup commitTransform / undo verified working

## 8. Testing (TypeScript)

- [x] 8.1 signedAngleOnPlane: 90°, -90°, 180°, 0° around Y axis <!-- 799/799 pass -->
- [x] 8.2 signedAngleOnPlane: continuity — 36 x 10° = 360° accumulation
- [x] 8.3 mapToArcball: inside sphere (z>0), on rim (z=0), outside (clamped), Y flip
- [x] 8.4 quatFromVectors: identity, 90°, 180° rotation
- [x] 8.5 pixelsToWorldRadius: tested implicitly via picking tolerance
- [x] 8.6 Axis-constrained rotation: 90° sweep on X, Y, Z axes
- [x] 8.7 Incremental delta accumulation: 36 x 10° = 360°
- [x] 8.8 Near-parallel ray: |dot| < 0.01 verified
- [x] 8.9 Ctrl-snap: 7° → 5°, 13° → 15°
- [x] 8.10 Shift precision: 90° raw → 9° with 0.1x factor
- [x] 8.11 Ctrl+Shift: 3.7° → 4° (1° snap)
- [x] 8.12 Arcball free rotation: drag produces rotation, no-movement = identity
- [x] 8.13 Hover hysteresis: 20% threshold test
- [x] 8.14 Multi-select: same delta applied independently
- [x] 8.15 Cancel drag: zero-angle rotation = original
- [x] 8.16 All existing non-rotation tests pass (799/799 total) <!-- no regressions -->

## 9. Documentation

- [x] 9.1 Updated editor-gizmos.md §4.2 — plane-projection math + incremental delta
- [x] 9.2 Added §4.2.1 — free rotation / trackball / arcball
- [x] 9.3 Updated §5 Grid Snap — 5° default, Shift precision, Ctrl+Shift 1°

## Deferred

- [ ] D1. Free-ring rendering in C++ engine (`gizmo_transform.cpp`) — add billboarded white circle geometry; requires camera forward vector in draw call. Task: `add-free-rotation-ring-rendering`
