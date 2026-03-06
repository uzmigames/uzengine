# Proposal: Blender-Style Arc Rotation Gizmo

## Why

The current rotation gizmo uses a screen-space horizontal linear slider to map pixel delta
to rotation angle. While stable, this behavior is unintuitive: dragging horizontally always
adds/subtracts angle regardless of where the mouse is relative to the gizmo. Users expect
to "grab" a ring and rotate it by sweeping the mouse around the gizmo center, exactly like
Blender, Unity, and Unreal Engine. The linear slider feels disconnected from the visual
ring geometry and does not match industry-standard muscle memory.

## What Changes

### 1. Replace linear slider with arc-sweep rotation (TypeScript)

Replace the current `deltaAngle = (screenX - startScreenX) * sensitivity` with an
atan2-based angular sweep around the screen-projected gizmo center:

- At drag start: project the gizmo world-space origin to screen space; store the initial
  angle from the projected center to the mouse position via `atan2(dy, dx)`
- On each mousemove: compute the current angle from the projected center to the mouse
  via `atan2(dy, dx)`; the delta is `currentAngle - startAngle`
- This makes the rotation follow the mouse's circular sweep around the gizmo — dragging
  clockwise rotates one way, counter-clockwise the other
- The rotation direction is determined by the dot product of the constrained world axis
  with the camera forward vector: if the axis points toward the camera, the screen-space
  angular sweep maps directly; if it points away, the sign is flipped. This ensures
  consistent behavior regardless of camera orientation.

### 2. Keep world-space axes (no change)

Rotation axes remain fixed to world-space X/Y/Z — already implemented.

### 3. Multi-select arc rotation (TypeScript)

Same arc-sweep delta applied to all selected nodes individually — same pattern as current
implementation, just with the new delta computation.

### 4. Ctrl-snap (no behavioral change)

Ctrl-snap to 15-degree increments remains the same.

## Impact

- **Affected code**: `vscode-uzengine/src/viewport-panel.ts` (rotation drag logic in mousemove + drag start)
- **Affected specs**: `docs/specs/editor/editor-gizmos.md` (rotation drag behavior description)
- **Affected tests**: `vscode-uzengine/src/__tests__/gizmo-controller.test.ts` (rotation test cases)
- **Breaking changes**: None (replaces the existing slider behavior)
- **User benefit**: Rotation feels natural — mouse sweeps around the ring to rotate, matching Blender exactly
