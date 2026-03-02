# Multi-Select Spec

## Overview

Multi-select allows the user to operate on several scene nodes simultaneously. Selection state is owned by a `SelectionManager` class in the TypeScript extension. All editor panels (Scene Tree, Inspector, Viewport gizmo) react to selection changes via a shared event emitter.

---

## 1. SelectionManager (TypeScript)

```typescript
class SelectionManager {
  private readonly selected = new Set<number>();  // node IDs

  /** Replace selection with a single node. */
  selectOnly(nodeId: number): void;

  /** Add a node to the current selection. */
  add(nodeId: number): void;

  /** Remove a node from the current selection. */
  remove(nodeId: number): void;

  /** Toggle a node in the current selection. */
  toggle(nodeId: number): void;

  /** Select a contiguous range (Scene Tree order). */
  selectRange(fromId: number, toId: number): void;

  /** Clear the entire selection. */
  clear(): void;

  /** Returns true if the node is selected. */
  has(nodeId: number): boolean;

  /** Returns an immutable snapshot of currently selected IDs. */
  get ids(): ReadonlySet<number>;

  /** Number of selected nodes. */
  get count(): number;

  /** Event fired whenever selection changes. */
  readonly onChanged: vscode.EventEmitter<ReadonlySet<number>>;
}
```

`SelectionManager` is instantiated once at extension activation and stored on the extension context. Every panel that needs selection state receives a reference to the same instance.

---

## 2. Input Gestures

### 2.1 Scene Tree Panel

| User action | Result |
|-------------|--------|
| Click a node | `selectOnly(nodeId)` — replaces selection |
| `Ctrl+click` a node | `toggle(nodeId)` — add or remove |
| `Shift+click` a node | `selectRange(anchorId, nodeId)` — contiguous block |
| Click empty area | `clear()` |

The Scene Tree highlights all selected rows simultaneously using VSCode's `TreeView.reveal` API with `select: true`. For multi-select, all selected tree items get the `selected` decoration (custom `ThemeIcon` badge).

The "anchor" for Shift+click is the last node touched by a plain click or Ctrl+click.

### 2.2 Viewport Panel

| User action | Result |
|-------------|--------|
| Left-click on object | `selectOnly(pickedNodeId)` |
| `Ctrl+left-click` on object | `toggle(pickedNodeId)` |
| Left-click on empty space | `clear()` |
| Drag a rectangle (no gizmo hit) | Box-select — selects all nodes whose screen-space bounds intersect the rectangle |

Box select calls:

```jsonc
{
  "fn": "editor.boxPick",
  "args": { "x": 100, "y": 80, "width": 200, "height": 150 }
}
// Response: { "nodeIds": [3, 5, 7] }
```

The engine renders the pick buffer and returns all unique node IDs within the screen rectangle.

---

## 3. Inspector — Shared Properties

When `SelectionManager.count > 1`, the Inspector shows only properties that are **common to all selected nodes**: same property name and same property type.

### 3.1 Computing Common Properties

```typescript
function commonProperties(
  nodeProps: Map<number, Map<string, PropertyDescriptor>>
): Map<string, PropertyDescriptor> {
  let common: Map<string, PropertyDescriptor> | undefined;
  for (const props of nodeProps.values()) {
    if (!common) {
      common = new Map(props);
    } else {
      for (const [key, desc] of common) {
        const other = props.get(key);
        if (!other || other.type !== desc.type) {
          common.delete(key);
        }
      }
    }
  }
  return common ?? new Map();
}
```

### 3.2 Mixed Values Display

If a shared property has **different values** across selected nodes, the input shows `"--"` (or an empty placeholder for numeric fields). Editing that field broadcasts the new value to all selected nodes.

```
Inspector — 3 nodes selected
─────────────────────────────────
▼ Transform
  Position  X [--  ] Y [0.0] Z [--  ]   ← X and Z differ across nodes
  Rotation  X [0.0 ] Y [0.0] Z [0.0 ]
  Scale     X [1.0 ] Y [1.0] Z [1.0 ]
─────────────────────────────────
```

### 3.3 Broadcasting Changes

When the user edits a field in multi-select mode, the extension creates a `BulkPropertyCommand`:

```typescript
const entries = [...selectionManager.ids].map(id => ({
  nodeId: id,
  key: 'intensity',
  oldValue: cachedProperties.get(id)?.get('intensity'),
  newValue: 2.5,
}));
undoStack.execute(new BulkPropertyCommand(entries));
```

This creates a single undoable action that reverts all nodes at once.

---

## 4. Scene Tree Visual State

All selected nodes are highlighted in the Scene Tree. VSCode's native tree does not natively support multi-highlight, so selected items receive a special tree item `description` badge `[selected]` and a `ThemeIcon` with a distinct color when the built-in highlight is insufficient.

Additionally, the extension tracks the "primary" selection (most recently selected node) for cases where a single node is needed (e.g. renaming, focus frame).

```
▼ World
  ▼ Buildings          ← not selected
    ■ House1           ← selected (blue highlight)
    ■ House2           ← selected (blue highlight)
    □ Warehouse        ← not selected
  □ Sun                ← not selected
  ■ Ground             ← selected (blue highlight)
```

---

## 5. Gizmo Behaviour

When multiple nodes are selected, the gizmo is placed at the **centroid** of all selected nodes' world positions:

```typescript
function centroid(positions: readonly Vec3[]): Vec3 {
  const sum = positions.reduce(
    (acc, p) => [acc[0] + p[0], acc[1] + p[1], acc[2] + p[2]],
    [0, 0, 0]
  );
  return [sum[0] / positions.length, sum[1] / positions.length, sum[2] / positions.length];
}
```

All drag operations apply the same world-space delta to every selected node:

```typescript
for (const id of selectionManager.ids) {
  const newPos = vec3Add(originalPositions.get(id)!, delta);
  await ipc.call('editor.setNodeTransformLive', { nodeId: id, position: newPos });
}
```

On `mouseup`, a single `BulkPropertyCommand` is pushed covering all moved nodes.

---

## 6. Bulk Operations

### 6.1 Delete

Context menu "Delete" or `Delete` key when the viewport/tree has focus:

```typescript
async function deleteSelected(): Promise<void> {
  const ids = [...selectionManager.ids];
  // Create removal commands in reverse depth-first order
  // (children before parents so parent removal doesn't cascade)
  const sortedIds = sortByDepthDescending(ids);
  const snapshots = await Promise.all(
    sortedIds.map(id => ipc.call('scene.getSubtree', { id }))
  );
  const cmd = new BulkRemoveCommand(sortedIds, snapshots);
  await undoStack.execute(cmd);
  selectionManager.clear();
}
```

### 6.2 Duplicate

Context menu "Duplicate" or `Ctrl+D`:

1. For each selected node, call `scene.getSubtree({id})` to capture the serialized subtree.
2. For each subtree, call `scene.restoreSubtree({parentId: node.parentId, subtree})` to create the clone.
3. Assign new names: `"House1"` → `"House1 (copy)"`.
4. Select only the duplicates.
5. Push a `BulkDuplicateCommand` to the undo stack.

### 6.3 Move to Parent

Context menu "Move to..." → shows a node picker:

1. User picks target parent node from a quick-pick list.
2. Extension creates `BulkReparentCommand` covering all selected nodes.
3. Executes via undo stack.

### 6.4 Group

Context menu "Group Selection" → creates a new `Node3D` at the centroid, reparents all selected nodes under it, pushes a `GroupCommand` to the undo stack.

---

## 7. IPC Additions

| IPC Call | Args | Response | Description |
|----------|------|----------|-------------|
| `editor.boxPick` | `{x, y, width, height}` (screen px) | `{nodeIds: number[]}` | GPU pick all nodes in rectangle |
| `editor.setSelection` | `{nodeIds: number[]}` | — | Sync selection to engine (for gizmo rendering) |

The `editor.setSelection` call is sent every time `SelectionManager.onChanged` fires, so the engine knows which nodes to gizmo-highlight.

---

## 8. Keyboard Shortcuts

| Shortcut | Action | Context |
|----------|--------|---------|
| `Ctrl+A` | Select all nodes | Viewport or Scene Tree focused |
| `Escape` | Clear selection | Viewport or Scene Tree focused |
| `Delete` | Delete selected nodes | Viewport or Scene Tree focused |
| `Ctrl+D` | Duplicate selected nodes | Viewport or Scene Tree focused |
| `G` | Group selected nodes | Viewport focused |
| `F` | Frame selected nodes in viewport | Viewport focused |

All shortcuts are registered as VSCode keybindings with `when` clauses.

---

## 9. Edge Cases

| Scenario | Behaviour |
|---------|-----------|
| Selected node is deleted externally (e.g. via IPC) | `SelectionManager` receives `scene.nodeRemoved` event and removes the ID from the set |
| All selected nodes deleted at once | `clear()` called after the bulk remove command executes |
| Selecting a parent that contains selected children | Both remain in the set independently; gizmo is at centroid of all selected |
| Scene reload | `clear()` called; Scene Tree refreshes |
| Node with no common properties selected | Inspector shows an empty panel with the message "No common properties" |

---

## 10. Godot / Unity Comparison

| Feature | Unity | Godot | UzEngine |
|---------|-------|-------|---------|
| Multi-select in hierarchy | Ctrl+click | Ctrl+click | Ctrl+click |
| Range select | Shift+click | Shift+click | Shift+click |
| Inspector mixed values | Shows `—` | Shows `<multiple values>` | Shows `--` |
| Gizmo pivot | Center, individual, or custom | Center of selection | Centroid |
| Bulk delete undo | Single undo entry | Single undo entry | `BulkRemoveCommand` |
