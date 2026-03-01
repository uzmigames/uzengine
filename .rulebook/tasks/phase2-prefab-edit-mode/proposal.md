# Proposal: phase2-prefab-edit-mode

## Why

As scene complexity grows, users need to reuse node subtrees across multiple scenes. Without a prefab system, duplicated content must be manually synchronized across every scene that uses it — a maintenance nightmare. Prefab Edit Mode closes this loop: users edit a prefab once and all instances in any scene update automatically. This is a critical workflow for any non-trivial game project.

## What Changes

- Engine C++: `scene.openPrefab`, `scene.savePrefab`, `scene.closePrefab`, `scene.reloadPrefabInstances`, `scene.getPrefabOverrides`, `scene.revertPrefabOverride`, `scene.revertAllPrefabOverrides` IPC handlers. Internal `editorState` flag distinguishes SCENE_EDIT vs PREFAB_EDIT. Checkerboard background mode via `editor.setBackground`.
- Scene file format: `[node_instance]` section support in `.uzscene` parser and serializer. Only overrides (values differing from prefab defaults) are written into the instance section.
- TypeScript extension: `EditorStateManager` class managing `mode: EditorMode` state machine. Breadcrumb display in viewport toolbar. Private `UndoStack` per prefab session. Exit/save prompts.
- Inspector: "Prefab Overrides" section showing per-property delta from prefab default; blue highlight for overridden properties; "Revert" button per override.
- Asset Browser context menu: "Edit Prefab" action on `.uzscene` files.
- Scene Tree context menu: "Edit Prefab" action on `node_instance` nodes.

## Impact

- Affected specs: `docs/specs/prefab-edit-mode.md` (new), `docs/specs/scene-system.md` (`node_instance` format), `docs/specs/editor-panels.md` (Inspector overrides section, Asset Browser context menu)
- Affected code: Engine scene loader/saver (`src/scene/`), IPC layer (`src/ipc/`), VSCode extension (`src/editor/editorStateManager.ts`, `src/panels/inspectorPanel.ts`, `src/panels/viewportPanel.ts`, `src/panels/assetBrowserPanel.ts`)
- Breaking change: NO — existing `.uzscene` files without `node_instance` sections are unaffected
- User benefit: Reusable prefabs; edit once, update everywhere; per-instance property overrides; clean scene files (only diffs stored)
