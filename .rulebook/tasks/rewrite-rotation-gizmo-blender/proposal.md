# Proposal: Rewrite Rotation Gizmo — Full Blender Reproduction

## Why

The current rotation gizmo implementation is a simplified screen-space arc-sweep that does
not match Blender's actual rotation behavior. It uses atan2 of screen pixel coordinates
around the projected center, which causes drift at extreme angles, produces inconsistent
feel across camera orientations, and lacks critical features: no free/trackball rotation
(outer white ring), no Shift precision mode, no plane-projected signed angle computation,
no hover hysteresis, and no incremental delta rotation. Users who are accustomed to Blender
find the current behavior unpredictable. This rewrite reproduces Blender's rotation gizmo
interaction model faithfully.

## What Changes

### 1. Axis-Constrained Rotation via Plane Projection (TypeScript)

Replace the screen-space atan2 arc-sweep with proper world-space math:

- On mousedown: intersect the mouse ray with the rotation plane (normal = world axis).
  Compute `v0 = normalize(hitPoint - pivot)` as the initial vector in the plane.
- On mousemove: intersect the new ray with the same plane. Compute `v1 = normalize(hitPoint - pivot)`.
  Compute signed angle: `deltaAngle = atan2(dot(axis, cross(vPrev, v1)), dot(vPrev, v1))`.
- Use **incremental** delta (vPrev -> v1 each frame) to avoid drift and support continuous rotation beyond 360 degrees.
- When the ray is nearly parallel to the plane (|denom| < eps), freeze rotation for that frame rather than producing wild values.

### 2. Free Rotation — Shoemake Arcball / Trackball (TypeScript)

Add a 4th handle: an outer white circle that faces the camera (billboarded).

- Map cursor position to a virtual sphere centered on the projected pivot.
- Compute quaternion that rotates start-vector to current-vector on the sphere.
- Apply the quaternion in camera space, then convert to world space.
- Must feel like Blender: stable near center, controlled near edge, no gimbal lock.

### 3. Hover Hysteresis + Priority Picking (TypeScript)

- Picking returns the ring with smallest ray-t (closest along ray) among rings within tolerance.
- Add hysteresis: if a ring is currently hovered, keep it unless another is significantly closer (20% threshold).
- Prevent rapid "flipping" between rings when cursor is near intersection.
- Convert pixel tolerance to world-space ring thickness: `ringThicknessWorld = PixelsToWorldRadius(pivot, 10px, camera)`.

### 4. Snapping + Precision Mode (TypeScript)

- **Ctrl**: snap to 5-degree increments (Blender default). Snap is relative to drag-start total angle, not per-frame.
- **Shift**: reduce sensitivity by 0.1x (Blender precision mode).
- **Ctrl+Shift**: snap to 1-degree increments.

### 5. State Machine (TypeScript)

Formalize the gizmo interaction as explicit states:
- Idle -> HoverAxisX / HoverAxisY / HoverAxisZ / HoverFree (based on picking)
- Hover* + MouseDown -> DragAxis / DragFree (locks axis until mouseup)
- Drag* + MouseUp -> Idle

### 6. Free Ring Rendering (C++ engine, deferred)

Add a view-facing white circle as the 4th ring in `gizmo_transform.cpp`. This requires
billboarding the ring geometry each frame to face the camera. **This can be deferred** to
a follow-up task — the TypeScript interaction math can be implemented first with the free
ring picking done in screen-space against a fixed pixel radius.

### 7. Multi-Select

Same incremental delta angle applied to each selected node from its own starting quaternion.
Position orbits around the shared pivot if pivot != node origin.

## Impact

- **Affected code**:
  - `vscode-uzengine/src/viewport-panel.ts` — complete rewrite of rotation drag logic, picking, hover, state machine
  - `vscode-uzengine/src/__tests__/gizmo-controller.test.ts` — new test suite for all rotation math
  - `src/renderer/gizmos/gizmo_transform.cpp` — add free-ring geometry (deferred to follow-up)
- **Affected specs**: `docs/specs/editor/editor-gizmos.md` — rotation sections rewritten
- **Breaking changes**: Replaces the current arc-sweep rotation behavior entirely
- **User benefit**: Rotation gizmo that feels identical to Blender — predictable, stable, professional
