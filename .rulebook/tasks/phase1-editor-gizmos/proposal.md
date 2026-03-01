# Proposal: phase1-editor-gizmos

## Why

Without gizmos, the only way to move, rotate, or scale a scene node is by typing values into the Inspector's numeric fields. This is slow, unintuitive, and makes spatial editing impractical. 3D transform gizmos are a fundamental editor interaction found in every 3D authoring tool; their absence would make UzEngine unusable as a scene editor.

## What Changes

- New DX12 render pass in the engine (guarded by `UZ_EDITOR`) that draws translate/rotate/scale gizmo geometry on top of the scene with no depth test against scene objects.
- New TypeScript module `src/editor/gizmoController.ts` that handles canvas mouse events, ray-casting from screen pixels to world space, axis hit-testing, drag mechanics for all three modes, and grid snap.
- New IPC handlers: `editor.setGizmo`, `editor.setGizmoHover`, `editor.setGizmoSpace`, `editor.setNodeTransformLive`, `editor.commitTransform`, `editor.getCamera`, `editor.pickNode`, `editor.boxPick`.
- Node picking: GPU pick buffer rendered at 1/4 resolution; `editor.pickNode` and `editor.boxPick` IPC handlers.
- Integration with `UndoStack`: `TransformCommand` pushed on `mouseup` via `editor.commitTransform`.
- Viewport toolbar buttons: Move (W), Rotate (E), Scale (R), Local/World toggle.

## Impact

- Affected specs: `docs/specs/editor-gizmos.md` (new), `docs/specs/editor-panels.md` (viewport section), `docs/specs/undo-redo.md` (TransformCommand), `docs/specs/multi-select.md` (centroid gizmo)
- Affected code: Engine renderer (`src/renderer/`), engine IPC layer (`src/ipc/`), VSCode extension viewport webview (`src/panels/viewportPanel.ts`)
- Breaking change: NO — additive only; gizmo pass is compiled out in non-editor builds
- User benefit: Direct mouse-driven 3D transform editing; click-to-select nodes in the viewport; all transforms integrate with the undo system
