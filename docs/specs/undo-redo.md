# Undo/Redo System Spec

## Overview

Every destructive editor action is wrapped in an `EditorCommand`. A central `UndoStack` in the TypeScript extension maintains a history of commands that can be replayed forward or reversed, giving users standard `Ctrl+Z` / `Ctrl+Shift+Z` behaviour across all editor operations.

This is equivalent to Godot's `UndoRedo` singleton, but implemented split across the TypeScript extension (stack ownership) and the C++ engine (state mutation).

---

## 1. Command Pattern

### 1.1 `EditorCommand` Interface (TypeScript)

```typescript
interface EditorCommand {
  readonly id: string;          // UUID assigned at creation time
  readonly label: string;       // Human-readable description, shown in menu
  execute(): Promise<void>;     // Apply the command (first time and on redo)
  undo(): Promise<void>;        // Revert the command
}
```

Every command sends an IPC call to the engine on `execute()` and a complementary IPC call on `undo()`. The engine stores no undo history — the TypeScript side is the sole owner of the stack and of the "before" values needed to revert.

### 1.2 Command Types

| Class | `label` example | `execute()` IPC | `undo()` IPC |
|-------|-----------------|-----------------|--------------|
| `SetPropertyCommand` | "Set intensity → 2.0" | `scene.setProperty({id, key, value})` | `scene.setProperty({id, key, oldValue})` |
| `AddNodeCommand` | "Add DirectionalLight" | `scene.addNode({type, parentId, ...})` | `scene.removeNode({id})` |
| `RemoveNodeCommand` | "Delete Sun" | `scene.removeNode({id})` | `scene.addNode({snapshot})` |
| `ReparentCommand` | "Move House1 → Buildings" | `scene.reparentNode({id, newParentId})` | `scene.reparentNode({id, oldParentId})` |
| `RenameNodeCommand` | "Rename Ground → Terrain" | `scene.setProperty({id, "name", newName})` | `scene.setProperty({id, "name", oldName})` |
| `BulkPropertyCommand` | "Set position (3 nodes)" | Multiple `scene.setProperty` in batch | Reverts all to saved old values |
| `TransformCommand` | "Move Player" | `scene.setProperty({id, "transform", newT})` | `scene.setProperty({id, "transform", oldT})` |

`RemoveNodeCommand` captures a full serialized snapshot of the removed subtree (output of `scene.getSubtree({id})`) before sending the remove IPC, so the exact node tree can be restored on undo.

---

## 2. UndoStack

### 2.1 Stack Structure (TypeScript)

```typescript
class UndoStack {
  private readonly MAX_SIZE = 100;
  private stack: EditorCommand[] = [];
  private pointer = -1;          // index of last executed command
  private dirty = false;         // scene has unsaved changes

  async execute(cmd: EditorCommand): Promise<void>;
  async undo(): Promise<void>;
  async redo(): Promise<void>;

  get canUndo(): boolean;
  get canRedo(): boolean;
  get isDirty(): boolean;
  markSaved(): void;             // called after scene.save

  readonly onChanged: vscode.EventEmitter<void>;
}
```

### 2.2 Stack Behaviour

- `execute(cmd)` — runs `cmd.execute()`, pushes onto stack at `pointer + 1`, truncates any redo tail, advances `pointer`, sets `dirty = true`, fires `onChanged`.
- `undo()` — if `canUndo`, runs `stack[pointer].undo()`, decrements `pointer`, fires `onChanged`.
- `redo()` — if `canRedo`, runs `stack[pointer + 1].execute()`, increments `pointer`, fires `onChanged`.
- When the stack reaches `MAX_SIZE` entries, the oldest command is dropped from index 0.
- `markSaved()` sets `dirty = false` and fires `onChanged`. The stack is not cleared — undo still works after a save.

### 2.3 VSCode Menu / Command Palette Entries

The standard VSCode Edit menu entries are wired via `package.json` contribution points:

```jsonc
// package.json — contributes.commands
{
  "command": "uzengine.undo",
  "title": "Undo",
  "category": "UzEngine"
},
{
  "command": "uzengine.redo",
  "title": "Redo",
  "category": "UzEngine"
}
```

```jsonc
// package.json — contributes.keybindings
{
  "command": "uzengine.undo",
  "key": "ctrl+z",
  "when": "uzengine.editorFocused"
},
{
  "command": "uzengine.redo",
  "key": "ctrl+shift+z",
  "when": "uzengine.editorFocused"
}
```

`uzengine.editorFocused` is a context key set to `true` whenever the viewport webview or any engine panel is focused. This prevents hijacking `Ctrl+Z` inside a regular text editor tab.

---

## 3. IPC Protocol

### 3.1 Engine-Side Requirements

The engine does **not** maintain an undo stack. It only needs to support the existing `scene.*` property set/get/add/remove IPC calls and one additional call:

```jsonc
// Returns full serialized subtree rooted at `id` (for RemoveNodeCommand snapshot)
{ "fn": "scene.getSubtree", "args": { "id": 42 } }
// Response: { "subtree": { ...uzscene JSON fragment... } }
```

```jsonc
// Restores a previously removed subtree under parent `parentId`
{ "fn": "scene.restoreSubtree", "args": { "parentId": 0, "subtree": { ... } } }
// Response: { "rootId": 42 }   ← restored root node's new id
```

### 3.2 Execution Flow

```
User action (e.g. drag position handle)
         │
         ▼
Extension creates SetPropertyCommand { id=5, key="position", old=[0,0,0], new=[1,2,3] }
         │
         ▼
UndoStack.execute(cmd)
    → cmd.execute() → IPC: scene.setProperty {id:5, key:"position", value:[1,2,3]}
    → Engine applies, re-renders
    → Stack: [..., cmd]  pointer → cmd
    → dirty = true
         │
         ▼
User presses Ctrl+Z
         │
         ▼
UndoStack.undo()
    → cmd.undo() → IPC: scene.setProperty {id:5, key:"position", value:[0,0,0]}
    → Engine reverts, re-renders
    → pointer decrements
```

---

## 4. Inspector Debounce

Inspector text/number fields must not create one undo entry per keystroke. A 300 ms debounce is applied:

```typescript
// Inside InspectorPanel webview message handler
private readonly DEBOUNCE_MS = 300;
private pendingTimer: NodeJS.Timeout | undefined;
private pendingOldValue: unknown;

onPropertyInput(nodeId: number, key: string, newValue: unknown): void {
  if (!this.pendingOldValue) {
    // Capture the pre-edit value before first keystroke
    this.pendingOldValue = this.cachedProperties.get(nodeId)?.get(key);
  }
  clearTimeout(this.pendingTimer);
  this.pendingTimer = setTimeout(() => {
    const cmd = new SetPropertyCommand(nodeId, key, this.pendingOldValue, newValue);
    this.undoStack.execute(cmd);
    this.pendingOldValue = undefined;
  }, this.DEBOUNCE_MS);
}
```

The engine receives live updates via `scene.setProperty` on every keystroke (for immediate visual feedback), but the undo entry is only committed after the debounce fires.

The live IPC path bypasses the `UndoStack` entirely — it calls `scene.setProperty` directly. Only the debounced timer creates and pushes a command.

---

## 5. Multi-Select Bulk Commands

When multiple nodes are selected and the user changes a shared property in the Inspector, a single `BulkPropertyCommand` is created:

```typescript
class BulkPropertyCommand implements EditorCommand {
  constructor(
    private readonly entries: ReadonlyArray<{
      nodeId: number;
      key: string;
      oldValue: unknown;
      newValue: unknown;
    }>
  ) {}

  async execute(): Promise<void> {
    for (const e of this.entries) {
      await ipc.call('scene.setProperty', { id: e.nodeId, key: e.key, value: e.newValue });
    }
  }

  async undo(): Promise<void> {
    for (const e of this.entries) {
      await ipc.call('scene.setProperty', { id: e.nodeId, key: e.key, value: e.oldValue });
    }
  }
}
```

This means `Ctrl+Z` reverts all affected nodes at once — the expected behaviour.

---

## 6. Scene Dirty State

### 6.1 Dirty Indicator

When `UndoStack.isDirty` is true, the status bar shows a dot: `● main.uzscene`. When clean (just saved or no commands executed), it shows `main.uzscene`.

### 6.2 Prompt on Play

Before launching play mode, the extension checks `undoStack.isDirty`. If true:

```
The scene has unsaved changes. Save before playing?
[ Save and Play ]  [ Play Without Saving ]  [ Cancel ]
```

### 6.3 Prompt on Close

`vscode.workspace.onWillSaveTextDocument` is not applicable here (the scene is not a text document). Instead, the extension listens to `vscode.workspace.onDidCloseTextDocument` and checks dirty state before the extension host shuts down via the deactivate hook.

---

## 7. Persistence and Limits

| Constraint | Value |
|-----------|-------|
| Max undo stack depth | 100 entries |
| Undo stack persists across saves | Yes (clear only on scene reload) |
| Undo stack cleared on scene load | Yes (`scene.load` resets stack) |
| Undo stack serialized to disk | No (session-only) |
| IPC timeout per command | 5 000 ms |

---

## 8. Edge Cases

| Scenario | Behaviour |
|---------|-----------|
| Engine crashes mid-command | `execute()` rejects; extension shows error; command is not pushed to stack |
| Node deleted while referenced in stack | `undo()` for RemoveNodeCommand uses `scene.restoreSubtree` which recreates the node with a new id; subsequent commands in the redo tail that referenced the old id become invalid and are dropped |
| Undo past scene load | Not possible — stack is cleared on `scene.load` |
| Two panels editing same property | Last write wins; both panels debounce independently |

---

## 9. Godot Comparison

| Concept | Godot | UzEngine |
|---------|-------|---------|
| Undo registry | `UndoRedo` singleton (GDScript/C++) | `UndoStack` TypeScript class |
| Command registration | `undo_redo.create_action(...)` | `new SetPropertyCommand(...)` |
| Batch | `begin_force_keep_in_merge_ends()` | `BulkPropertyCommand` |
| Dirty tracking | `EditorPlugin.get_undo_redo().get_version()` | `UndoStack.isDirty` |
| Max history | Configurable in Editor Settings | Hardcoded 100 |
