# Proposal: Add Free Rotation Ring Rendering (C++ Engine)

## Why

The Blender-style rotation gizmo rewrite (task `rewrite-rotation-gizmo-blender`) adds a
free rotation / trackball handle that requires an outer white circle ring to be rendered in
the 3D viewport. This ring must be billboarded (always facing the camera) and rendered as
part of the existing gizmo transform pipeline. The TypeScript interaction code handles
picking and rotation math in screen-space, but the visual ring needs engine-side rendering.

## What Changes

- Add a 4th ring (white, camera-facing) to `gizmo_transform.cpp` geometry.
- The free ring is a circle in the plane perpendicular to the camera forward vector,
  centered at the gizmo pivot, with radius slightly larger than axis rings (2.0 * gScale).
- Geometry must be rebuilt or transformed each frame based on camera orientation.
- `drawMode()` updated to draw the free ring with its own highlight state.

## Impact

- **Affected code**: `src/renderer/gizmos/gizmo_transform.cpp`, `gizmo_transform.h`
- **Affected specs**: `docs/specs/editor/editor-gizmos.md` (visual ring description)
- **Breaking changes**: None
- **User benefit**: Visual free-rotation handle matching Blender's outer grey/white ring
