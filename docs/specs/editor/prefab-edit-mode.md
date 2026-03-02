# Prefab Edit Mode Spec

## Overview

A prefab is a reusable `.uzscene` file that can be instanced multiple times into other scenes. Prefab Edit Mode lets the user open a prefab in isolation, edit it, save it, and return to the original scene — with all instances of the prefab in the main scene updated automatically.

This is equivalent to Unity's Prefab Mode or Unreal's template actor editing workflow.

---

## 1. What Is a Prefab?

A prefab is any `.uzscene` file that is referenced by another scene using a special `[node_instance]` section:

```ini
; main.uzscene
[node_instance]
name    = EnemyA
type    = Node3D
source  = res://prefabs/enemy.uzscene
position = 5.0, 0.0, 3.0
```

A scene file becomes a prefab simply by being referenced via `node_instance`. There is no separate file type — the distinction is contextual.

---

## 2. Entering Prefab Edit Mode

### 2.1 Trigger

The user double-clicks a `.uzscene` file in the Asset Browser that is used as a prefab. Alternatively, the context menu offers "Edit Prefab" on a `node_instance` node selected in the Scene Tree.

### 2.2 Engine-Side: Scene Isolation

The extension sends:

```jsonc
{ "fn": "scene.openPrefab", "args": { "path": "res://prefabs/enemy.uzscene" } }
```

The engine responds by:

1. Serializing and caching the current main scene state in memory.
2. Clearing the scene graph.
3. Loading the prefab file into the scene graph.
4. Setting an internal flag `editorState = PREFAB_EDIT`.
5. Rendering only the prefab contents in the viewport.

Response:

```jsonc
{ "prefabPath": "res://prefabs/enemy.uzscene", "rootNodeId": 1 }
```

### 2.3 Extension-Side

On receiving the response, the extension:

1. Updates `EditorStateManager.mode` from `'scene'` to `'prefab'`.
2. Sets `EditorStateManager.activePrefabPath` to the prefab path.
3. Refreshes the Scene Tree to show the prefab node hierarchy.
4. Displays the breadcrumb indicator in the viewport toolbar.
5. Pushes the main scene's `UndoStack` state aside and creates a fresh `UndoStack` for the prefab session.

---

## 3. Visual Indicator

The viewport toolbar shows a breadcrumb when in prefab edit mode:

```
┌────────────────────────────────────────────────────────────────┐
│  [3D]  [2D]    Editing: res://prefabs/enemy.uzscene    [Exit]  │
│                            ↑ breadcrumb                        │
├────────────────────────────────────────────────────────────────┤
│                                                                │
│                    PREFAB VIEWPORT                             │
│               (grey checkerboard background)                   │
│                                                                │
└────────────────────────────────────────────────────────────────┘
```

- The background changes to a **grey checkerboard** pattern (configurable) to visually distinguish prefab mode from normal editing.
- The normal play controls are hidden; only "Exit Prefab Mode" is shown.
- The status bar shows `● enemy.uzscene (prefab)`.

The engine applies the checkerboard via:

```jsonc
{ "fn": "editor.setBackground", "args": { "type": "checkerboard" } }
```

---

## 4. Editing Inside Prefab Mode

All standard editor operations work inside prefab mode:

- Scene Tree shows the prefab's node hierarchy.
- Inspector edits node properties.
- Gizmos translate/rotate/scale nodes.
- Undo/Redo operates on the prefab's private `UndoStack`.
- New nodes can be added to or removed from the prefab.

All IPC calls use the same `scene.*` protocol — the engine internally routes them to the prefab scene rather than the main scene.

---

## 5. Saving the Prefab

Changes to the prefab are **not saved automatically during editing**. The user must explicitly save:

- `Ctrl+S` while in prefab edit mode → saves the prefab file.
- "Exit Prefab Mode" button → prompts to save if dirty.

### 5.1 Save IPC

```jsonc
{ "fn": "scene.savePrefab", "args": { "path": "res://prefabs/enemy.uzscene" } }
// Response: { "ok": true }
```

This serializes the current in-engine scene graph back to the `.uzscene` file at the given path.

### 5.2 Instance Reload Notification

After saving, the extension checks if the main scene has any instances of this prefab. If so, it prompts:

```
enemy.uzscene was saved. Reload instances in main.uzscene?
[ Reload Instances ]  [ Later ]
```

If the user chooses "Reload Instances":

```jsonc
{ "fn": "scene.reloadPrefabInstances", "args": { "prefabPath": "res://prefabs/enemy.uzscene" } }
```

The engine updates all `node_instance` nodes referencing the saved prefab, re-applying overrides from the main scene.

---

## 6. Exiting Prefab Edit Mode

### 6.1 User Action

The user clicks the "Exit Prefab Mode" button or runs the `uzengine.exitPrefabMode` command.

### 6.2 Dirty Check

If the prefab has unsaved changes (`undoStack.isDirty`):

```
Unsaved changes in enemy.uzscene.
[ Save and Exit ]  [ Discard and Exit ]  [ Cancel ]
```

### 6.3 Scene Restore

After saving (or discarding), the extension sends:

```jsonc
{ "fn": "scene.closePrefab", "args": {} }
```

The engine:

1. Unloads the prefab scene from the graph.
2. Restores the cached main scene state.
3. Clears `editorState = SCENE_EDIT`.
4. Re-renders the main scene.

Response:

```jsonc
{ "ok": true }
```

The extension then:

1. Restores the main scene's `UndoStack`.
2. Resets `EditorStateManager.mode` to `'scene'`.
3. Refreshes the Scene Tree to show the main scene hierarchy.
4. Resets the viewport breadcrumb and background.

---

## 7. Override Panel

When a `node_instance` node is selected in the main scene's Inspector, the Inspector shows a special "Prefab Overrides" section listing which properties differ from the prefab's default values:

```
Inspector — EnemyA [node_instance: res://prefabs/enemy.uzscene]
─────────────────────────────────────────────────────────────
▼ Node
  Name         [EnemyA        ]
  Source       res://prefabs/enemy.uzscene (read-only)

▼ Transform
  Position     X [5.0 ] Y [0.0] Z [3.0]   ← always editable (instance override)
  Rotation     X [0.0 ] Y [45.] Z [0.0]   ← highlighted BLUE (differs from prefab)
  Scale        X [1.0 ] Y [1.0] Z [1.0]

▼ Prefab Overrides                        ← collapsible section
  rotation.y   45° (prefab: 0°)           [↺ Revert]
─────────────────────────────────────────────────────────────
```

Properties that differ from the prefab default are **highlighted in blue**. Each override has a "Revert" button that resets it to the prefab default.

### 7.1 IPC for Overrides

```jsonc
{ "fn": "scene.getPrefabOverrides", "args": { "instanceId": 12 } }
// Response:
{
  "overrides": [
    { "key": "rotation.y", "instanceValue": 45.0, "prefabDefault": 0.0 },
    { "key": "light.intensity", "instanceValue": 2.0, "prefabDefault": 1.0 }
  ]
}
```

```jsonc
// Revert a single override to prefab default
{ "fn": "scene.revertPrefabOverride", "args": { "instanceId": 12, "key": "rotation.y" } }
```

```jsonc
// Revert all overrides
{ "fn": "scene.revertAllPrefabOverrides", "args": { "instanceId": 12 } }
```

---

## 8. IPC Summary

| IPC Call | Direction | Description |
|----------|-----------|-------------|
| `scene.openPrefab` | Extension → Engine | Enter prefab edit mode, load prefab |
| `scene.savePrefab` | Extension → Engine | Serialize prefab to `.uzscene` file |
| `scene.closePrefab` | Extension → Engine | Exit prefab edit mode, restore main scene |
| `scene.reloadPrefabInstances` | Extension → Engine | Update all instances after prefab save |
| `scene.getPrefabOverrides` | Extension → Engine | List override properties on an instance |
| `scene.revertPrefabOverride` | Extension → Engine | Reset one override to prefab default |
| `scene.revertAllPrefabOverrides` | Extension → Engine | Reset all overrides on an instance |

---

## 9. State Machine

```
            ┌──────────────────────────────────────────┐
            │         SCENE_EDIT (normal mode)         │
            └─────────────────┬────────────────────────┘
                              │  scene.openPrefab({path})
                              ▼
            ┌──────────────────────────────────────────┐
            │         PREFAB_EDIT mode                 │
            │  - Prefab scene loaded                   │
            │  - Fresh UndoStack                       │
            │  - Breadcrumb visible                    │
            │  - Background: checkerboard              │
            └──────────────┬───────────────────────────┘
                           │
           ┌───────────────┼───────────────┐
           │               │               │
           ▼               ▼               ▼
      Ctrl+S          Exit (save)    Exit (discard)
           │               │               │
           ▼               ▼               ▼
    scene.savePrefab → scene.closePrefab  scene.closePrefab
           │               │               │
           └───────────────┴───────────────┘
                           │
                           ▼
            ┌──────────────────────────────────────────┐
            │     SCENE_EDIT (main scene restored)     │
            └──────────────────────────────────────────┘
```

---

## 10. EditorStateManager (TypeScript)

```typescript
type EditorMode = 'scene' | 'prefab' | 'play';

interface EditorStateManager {
  readonly mode: EditorMode;
  readonly activePrefabPath: string | undefined;
  readonly activeScenePath: string | undefined;

  enterPrefabMode(prefabPath: string): Promise<void>;
  exitPrefabMode(save: boolean): Promise<void>;
  enterPlayMode(): Promise<void>;
  exitPlayMode(): Promise<void>;

  readonly onModeChanged: vscode.EventEmitter<EditorMode>;
}
```

All panels subscribe to `onModeChanged` and update their UI accordingly (hide play controls in prefab mode, show breadcrumb, etc.).

---

## 11. File Format: node_instance Reference

```ini
; main.uzscene
[node_instance]
name        = EnemyA
type        = Node3D
source      = res://prefabs/enemy.uzscene
; Overrides (diff from prefab defaults):
rotation    = 0.0, 45.0, 0.0
```

Only properties that differ from the prefab defaults are written into the instance section. Properties not listed are inherited from the prefab file. This keeps the main scene file clean and ensures updates to the prefab propagate automatically.

---

## 12. Out of Scope (Phase 2)

- Nested prefabs (a prefab instantiating another prefab) — deferred.
- Prefab variants (a prefab that inherits from another prefab with structural overrides) — deferred.
- Prefab diff visualization in a separate panel — deferred.
- Auto-save of prefab on every change — by design, manual save only.

---

## 13. Unity / Godot Comparison

| Feature | Unity Prefab Mode | Godot Scene Instancing | UzEngine |
|---------|-------------------|----------------------|---------|
| Edit in isolation | Yes (separate scene view) | Yes (open scene file) | Same process, scene swap |
| Override highlighting | Blue in Inspector | Yellow dot in Inspector | Blue text in Inspector |
| Revert override | Button per property | Right-click in Inspector | "Revert" button per override |
| Nested prefabs | Yes (Unity 2018.3+) | Yes (inherited scenes) | Phase 3+ |
| Auto-save | No (explicit apply) | No (explicit save) | No (explicit save) |
| Apply to all instances | "Apply" button | Scene is the source | `scene.reloadPrefabInstances` |
