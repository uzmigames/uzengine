# Proposal: Editor Transform Gizmos + Selection Outline (Phase 2)

## Why

The engine has no way to interactively manipulate objects in the viewport. Users must edit .uzscene JSON files by hand to change positions/rotations/scales. This blocks all editor workflow: level design, lighting setup, camera placement. The `--editor` mode exists (`SetEditorMode(true)`) with an editor grid, but no selection or manipulation tools. Without gizmos, the engine is a viewer, not an editor.

UE5-style transform gizmos are the #1 expected editor feature. Selection outline provides visual feedback for which object is being manipulated. These two systems together enable the minimum viable editor interaction loop: click object → see highlight → drag gizmo → see transform change in real-time.

## What Changes

### New Systems

1. **GizmoRenderer** (`runtime/editor/gizmo/`) — Renders translate/rotate/scale gizmo handles as colored lines/arcs/cubes in the overlay pass. Matches UE5 visual style: RGB = XYZ axes, yellow/cyan/magenta = plane handles, white = screen-space uniform scale.

2. **GizmoInteraction** (`runtime/editor/gizmo/`) — Handles mouse raycasting against gizmo geometry, drag tracking, snapping, and transform computation. Produces delta transforms applied to the selected entity's SceneComponent.

3. **SelectionManager** (`runtime/editor/selection/`) — Tracks the currently selected entity. Provides click-to-select via mouse ray → AABB hit test against scene meshes. Maintains selection state (single select, clear, cycle).

4. **OutlinePass** (`runtime/editor/outline/`) — Post-process stencil-based outline renderer. Selected objects are re-drawn to a stencil buffer, then a fullscreen pass detects stencil edges and draws a colored outline (default: orange, UE5 style).

5. **EditorInputHandler** (`runtime/editor/input/`) — Routes mouse/keyboard events to gizmo interaction or selection. Handles mode switching (W=translate, E=rotate, R=scale), space toggle (world/local), and snap settings.

### Refactoring

- `frame_overlay.cpp` — Add gizmo draw call after editor grid, before stats overlay.
- `uzengine.cpp` — Wire `--editor` flag to `SetEditorMode(true)` and enable editor input handler.
- `scene_renderer.h` — Add `GetSelectedEntity()` / `SetSelectedEntity()` for outline pass.

## Impact
- Affected specs: `docs/specs/editor-gizmos.md` (existing, needs update), `docs/specs/editor-panels.md`
- Affected code: `runtime/editor/` (new), `runtime/engine/renderer/frame/frame_overlay.cpp`, `runtime/tools/uzengine.cpp`
- Breaking change: NO — editor mode is opt-in via `--editor` flag
- User benefit: Interactive object manipulation in the viewport — no more JSON editing for transforms
- Enables: Inspector panel (reads/writes selected entity), multi-select, undo/redo, prefab editing
