# Proposal: Fix Rotation Gizmo Behavior

## Why

The current rotation gizmo uses world-space arc intersection with signed angle computation
between start/current vectors on the rotation plane. This approach is unstable and unintuitive:
- The rotation direction flips unpredictably when the mouse crosses the gizmo origin
- The angle wraps around at 180 degrees causing sudden jumps
- Near edge-on camera angles the plane intersection becomes numerically unstable
- The user cannot predict which direction the rotation will go based on mouse movement

The rotation gizmo needs to behave like a linear slider: dragging right (screen +X) always
adds angle, dragging left (screen -X) always subtracts. This is the standard behavior in
Blender, Unity, and Unreal Engine for constrained single-axis rotation.

## What Changes

### 1. Replace arc-angle rotation with screen-space linear drag (TypeScript)

Replace the current rotation drag logic in `viewport-panel.ts` that computes signed angles
via `cross3`/`dot3` on the rotation plane with a simple screen-space delta approach:

- Track the initial screen-space mouse position at drag start
- On each mousemove, compute the horizontal screen-space delta from the drag start
- Map the pixel delta to an angle: `deltaAngle = deltaPixels * sensitivity`
- Apply as cumulative rotation: `newAngle = startAngle + deltaAngle`
- Sensitivity constant: approximately 0.5 degrees per pixel (PI/360 radians per pixel)

### 2. Force world-space axes for rotation gizmo

The rotation gizmo axes SHALL always remain fixed to world-space (X/Y/Z) regardless of
the object's current orientation. This prevents gimbal-lock confusion and ensures that
the X axis ring always rotates around world X, etc.

The gizmo rendering on the C++ side already uses world-space axes (the transform gizmo
geometry is not rotated by the node's orientation). This task ensures the TypeScript drag
logic also uses fixed world-space axis directions.

### 3. Multi-select rotation support

When multiple nodes are selected, the same screen-delta rotation SHALL be applied
to all selected nodes individually around the world-space axis.

## Impact

- **Affected specs**: `docs/specs/editor/editor-gizmos.md` (rotation drag behavior)
- **Affected code**: `vscode-uzengine/src/viewport-panel.ts` (drag logic in mousemove handler)
- **Breaking changes**: None (rotation was already broken/unusable)
- **User benefit**: Predictable, consistent rotation control matching industry-standard editors
