# Asset Drag & Drop Spec

## Overview

Drag & drop of assets into the scene is a fundamental editor UX feature. Because UzEngine runs **inside VSCode** (not a standalone window), the implementation must work within the constraints of the VSCode Extension API. This document specifies the three distinct DnD scenarios and their solutions.

---

## VSCode DnD Constraints

| Scenario | Native VSCode API | Solution |
|----------|------------------|----------|
| Scene Tree → Scene Tree (re-parent) | ✅ `TreeDragAndDropController` | Native |
| Asset Browser → Scene Tree (assign mesh/material) | ⚠️ Partial — same TreeView only | IPC message on drop |
| Asset Browser → Viewport WebView (place in world) | ❌ Not supported (WebView sandboxed) | Phantom drag + postMessage |
| OS File Explorer → Asset Browser | ❌ No direct API | `FileSystemWatcher` auto-import |
| Asset Browser → Inspector property slot | ⚠️ WebView receives no DnD events | Drag token via postMessage |

**Root cause:** VSCode WebViews run in a sandboxed iframe. The OS drag event that enters a WebView boundary is dropped silently — the WebView's JavaScript never receives a `dragover` or `drop` event from a `TreeItem`.

---

## Scenario 1: Scene Tree Re-parenting (TreeView → TreeView)

**Native support** via `vscode.TreeDragAndDropController` (VSCode 1.89+).

### Registration

```typescript
// extension/src/panels/sceneTree.ts
const dndController: vscode.TreeDragAndDropController<SceneTreeItem> = {
    dropMimeTypes: ['application/vnd.uzengine.scenetree'],
    dragMimeTypes: ['application/vnd.uzengine.scenetree'],

    handleDrag(source, dataTransfer) {
        dataTransfer.set(
            'application/vnd.uzengine.scenetree',
            new vscode.DataTransferItem(source.map(n => n.nodeId))
        );
    },

    async handleDrop(target, dataTransfer) {
        const nodeIds: number[] = dataTransfer
            .get('application/vnd.uzengine.scenetree')!.value;

        for (const id of nodeIds) {
            await ipc.call('scene.reparentNode', {
                nodeId: id,
                newParentId: target?.nodeId ?? 0  // 0 = scene root
            });
        }
        sceneTreeProvider.refresh();
    }
};

vscode.window.createTreeView('uzengine.sceneTree', {
    treeDataProvider: sceneTreeProvider,
    dragAndDropController: dndController,
});
```

### IPC: `scene.reparentNode`

```json
// Request
{ "fn": "scene.reparentNode", "args": { "nodeId": 42, "newParentId": 7 } }

// Response
{ "ok": true }
// or
{ "ok": false, "error": "Circular parent" }
```

**Engine side** (`scene_manager.c`): validates no circular reference, detaches node from old parent, inserts as last child of new parent, marks scene dirty.

---

## Scenario 2: Asset Browser → Scene Tree (Assign Asset to Node Slot)

**Use case:** User drags a `.glb` mesh from Asset Browser and drops it onto a `StaticMesh` node in Scene Tree to replace its mesh. Or drags a `.uzmat` onto a node to assign a material.

**Problem:** Both are `TreeView` providers but registered to different views — cross-view DnD isn't supported by VSCode native DnD.

### Solution: Drag Token Protocol

```
┌─ Asset Browser (TreeView) ──────────┐   ┌─ Scene Tree (TreeView) ──────────┐
│                                     │   │                                  │
│  User initiates drag on asset item  │   │  Scene Tree is listening for     │
│  → sets drag token in DataTransfer  │──▶│  MIME type: application/vnd...   │
│                                     │   │                                  │
└─────────────────────────────────────┘   └──────────────────────────────────┘
```

Both providers declare they accept each other's MIME types:

```typescript
// Asset Browser drag source
dragMimeTypes: ['application/vnd.uzengine.asset']

// Scene Tree drop target — also accepts assets
dropMimeTypes: [
    'application/vnd.uzengine.scenetree',  // re-parenting
    'application/vnd.uzengine.asset',       // assign asset to node
]
```

```typescript
// sceneTree.ts — handleDrop extended
async handleDrop(target, dataTransfer) {
    // Re-parent case (existing)
    const treeData = dataTransfer.get('application/vnd.uzengine.scenetree');
    if (treeData) { /* ... */ return; }

    // Asset assignment case
    const assetData = dataTransfer.get('application/vnd.uzengine.asset');
    if (assetData && target) {
        const asset: AssetDragPayload = assetData.value;
        await ipc.call('scene.assignAsset', {
            nodeId: target.nodeId,
            assetPath: asset.resPath,   // e.g. "res://meshes/hero.glb"
            assetType: asset.type,       // "Mesh" | "Material" | "Texture" | "AudioClip"
        });
        sceneTreeProvider.refresh();
    }
}
```

### IPC: `scene.assignAsset`

```json
// Request
{
    "fn": "scene.assignAsset",
    "args": {
        "nodeId": 15,
        "assetPath": "res://meshes/house.glb",
        "assetType": "Mesh"
    }
}

// Response
{ "ok": true, "componentAdded": "CMeshRenderer" }
// Engine auto-adds missing component if node doesn't have one yet
```

**Engine side:** if the target node lacks a `CMeshRenderer`, it is auto-created. The mesh resource handle is loaded via `resource_manager_load_mesh()` and assigned. Scene is marked dirty.

---

## Scenario 3: Asset Browser → Viewport (Place Asset in World)

**Use case:** User drags a `.glb` from Asset Browser and drops it onto the 3D viewport to place it at the world position under the cursor.

**Problem:** The viewport is a `WebviewPanel` (sandboxed iframe). TreeView DnD events never reach the WebView's DOM.

### Solution: Phantom Drag via postMessage

The extension intercepts the drag at the **TreeView level** and coordinates placement through a two-step IPC:

```
Step 1: User starts dragging asset from Asset Browser
        → Extension sets a "pending drop" state (in-memory, not WebView yet)

Step 2: WebView detects mouse enter + button held
        → postMessage to extension: { type: 'dragEnter' }
        → Extension shows drop cursor overlay in WebView

Step 3: User releases mouse over viewport canvas (mouseup in WebView)
        → WebView sends: { type: 'assetDropped', x, y }  (canvas pixel coords)
        → Extension calls IPC: editor.placeAsset { resPath, screenX, screenY }

Step 4: Engine raycasts screen coord → world position
        → Creates node at hit point, assigns asset, returns nodeId
        → Extension selects new node in Scene Tree
```

#### Extension Side

```typescript
// dragDropCoordinator.ts
export class DragDropCoordinator {
    private pendingAsset: AssetDragPayload | null = null;

    startDrag(asset: AssetDragPayload) {
        this.pendingAsset = asset;
        // Notify viewport WebView to enter "accept drop" mode
        viewportPanel.webview.postMessage({ type: 'dragStarted', asset });
    }

    endDrag() {
        this.pendingAsset = null;
        viewportPanel.webview.postMessage({ type: 'dragEnded' });
    }

    async handleViewportDrop(x: number, y: number) {
        if (!this.pendingAsset) return;
        const asset = this.pendingAsset;
        this.endDrag();

        const result = await ipc.call('editor.placeAsset', {
            resPath: asset.resPath,
            assetType: asset.type,
            screenX: x,
            screenY: y,
        });

        if (result.ok) {
            sceneTreeProvider.refresh();
            sceneTreeProvider.select(result.nodeId);
        }
    }
}
```

#### WebView Side (viewport.html)

```javascript
// viewport.js — inside WebviewPanel HTML
let dropModeActive = false;

window.addEventListener('message', (e) => {
    if (e.data.type === 'dragStarted') {
        dropModeActive = true;
        canvas.classList.add('drop-active'); // green border overlay
    }
    if (e.data.type === 'dragEnded') {
        dropModeActive = false;
        canvas.classList.remove('drop-active');
    }
});

canvas.addEventListener('mouseup', (e) => {
    if (dropModeActive) {
        vscode.postMessage({
            type: 'assetDropped',
            x: e.offsetX,
            y: e.offsetY
        });
    }
});
```

### IPC: `editor.placeAsset`

```json
// Request
{
    "fn": "editor.placeAsset",
    "args": {
        "resPath": "res://meshes/house.glb",
        "assetType": "Mesh",
        "screenX": 512,
        "screenY": 300
    }
}

// Response
{
    "ok": true,
    "nodeId": 47,
    "worldPos": [3.5, 0.0, -2.1]
}
```

**Engine side pipeline:**
1. Raycast screen coordinate against scene geometry (reuses `editor.pick` ray)
2. If ray hits geometry → place at hit position
3. If ray misses → place at `camera_forward * 5.0` units from camera
4. Create `Node3D`, add `CMeshRenderer` (or `CStaticMesh`), assign resource
5. Return new `nodeId` and world position

### Visual Feedback in Viewport

While dragging over the viewport, the WebView shows:
- **Green border** around canvas edge (drop-active overlay)
- **Ghost mesh** at cursor position (engine renders a semi-transparent preview) — optional, Phase 2 enhancement

```json
// Optional: engine renders a ghost preview while dragging
{ "fn": "editor.dragPreview", "args": { "resPath": "res://meshes/house.glb", "screenX": 512, "screenY": 300 } }
// Engine renders ghost mesh at world position, updates each frame until drop or cancel
{ "fn": "editor.dragPreviewCancel" }
```

---

## Scenario 4: OS File Explorer → Asset Browser (OS-Level Import)

**Use case:** User drags `.glb`, `.png`, `.wav`, or `.fbx` from Windows File Explorer into the Asset Browser panel.

**Problem:** VSCode extensions cannot intercept OS-level drag events into a TreeView. The OS drag never surfaces to the extension JS layer.

### Solution A: FileSystemWatcher Auto-Import (Primary)

Monitor the `assets/` directory. Any new file is auto-imported.

```typescript
// assetWatcher.ts
const watcher = vscode.workspace.createFileSystemWatcher(
    new vscode.RelativePattern(workspaceRoot, 'assets/**/*')
);

watcher.onDidCreate(async (uri) => {
    const ext = path.extname(uri.fsPath).toLowerCase();
    if (IMPORTABLE_EXTENSIONS.includes(ext)) {
        await ipc.call('asset.import', { filePath: uri.fsPath });
        assetBrowserProvider.refresh();
        vscode.window.showInformationMessage(`Asset imported: ${path.basename(uri.fsPath)}`);
    }
});
```

```typescript
const IMPORTABLE_EXTENSIONS = ['.glb', '.gltf', '.fbx', '.png', '.jpg', '.ktx2', '.wav', '.ogg', '.mp3'];
```

**User workflow:**
1. User copies/moves file into `<project>/assets/` folder in OS File Explorer
2. `FileSystemWatcher` fires `onDidCreate`
3. Extension calls `asset.import` IPC
4. Asset Browser refreshes, new asset appears

### Solution B: Import File Button (Secondary)

Asset Browser toolbar has an **Import File** button (`+`) that opens `vscode.window.showOpenDialog`:

```typescript
// assetBrowserProvider.ts
async importFile() {
    const uris = await vscode.window.showOpenDialog({
        canSelectMany: true,
        filters: {
            'Assets': ['glb', 'gltf', 'fbx', 'png', 'jpg', 'ktx2', 'wav', 'ogg', 'mp3'],
        },
        openLabel: 'Import Asset'
    });

    if (!uris) return;

    for (const uri of uris) {
        // Copy to assets/ directory, then trigger import
        const destPath = path.join(workspaceRoot, 'assets', path.basename(uri.fsPath));
        await vscode.workspace.fs.copy(uri, vscode.Uri.file(destPath));
        await ipc.call('asset.import', { filePath: destPath });
    }

    this.refresh();
}
```

### Solution C: Right-Click Context Menu (Tertiary)

Asset Browser context menu on any folder:

```
▶ assets/
  ├── meshes/
  │   └── [Right-click] → "Import File Here..."
  └── textures/
```

Registered via `contributes.menus` in `package.json`:

```json
{
    "contributes": {
        "menus": {
            "view/item/context": [
                {
                    "command": "uzengine.importAssetHere",
                    "when": "view == uzengine.assetBrowser && viewItem == folder",
                    "group": "import"
                }
            ]
        }
    }
}
```

---

## Scenario 5: Asset Browser → Inspector Property Slot

**Use case:** User drags a material from Asset Browser and drops it onto the "Material" field in the Inspector panel.

**Problem:** Inspector is a WebView (rich property editing). WebView cannot receive DnD events from TreeView.

### Solution: Click-to-Assign Mode

Rather than true DnD, use a **click-to-assign** workflow triggered by a right-click context menu on the asset:

```
Asset Browser: [Right-click mesh.glb]
  → "Assign to selected node's Mesh slot"
  → Calls: scene.assignAsset { nodeId: currentSelection, assetPath, assetType }
```

Additionally, Inspector property slots show an **asset picker button** `[...]`:

```
Inspector:
┌─────────────────────────────────────────┐
│ CMeshRenderer                           │
│   Mesh:     [house.glb          ] [...]  │  ← click [...] opens asset picker
│   Material: [default.uzmat      ] [...]  │
└─────────────────────────────────────────┘
```

The `[...]` button calls `vscode.window.showQuickPick` populated with assets of the matching type from the Asset Browser registry.

---

## Implementation Files

```
extension/src/
├── dragDropCoordinator.ts      ← Phantom drag state machine
├── panels/
│   ├── sceneTree.ts            ← TreeDragAndDropController (re-parent + asset assign)
│   ├── assetBrowser.ts         ← Drag source, import file button
│   └── viewport/
│       ├── viewportPanel.ts    ← postMessage relay to dragDropCoordinator
│       └── viewport.html       ← mouseup → 'assetDropped' message
├── assetWatcher.ts             ← FileSystemWatcher auto-import
└── ipc/
    └── ipcClient.ts            ← scene.reparentNode, scene.assignAsset,
                                   editor.placeAsset, asset.import
```

---

## IPC Summary

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `scene.reparentNode` | `nodeId, newParentId` | `ok` | Re-parent node in tree |
| `scene.assignAsset` | `nodeId, assetPath, assetType` | `ok, componentAdded` | Assign asset to node component |
| `editor.placeAsset` | `resPath, assetType, screenX, screenY` | `ok, nodeId, worldPos` | Raycast + create node at world pos |
| `editor.dragPreview` | `resPath, screenX, screenY` | `ok` | Render ghost preview (optional) |
| `editor.dragPreviewCancel` | — | `ok` | Cancel ghost preview |
| `asset.import` | `filePath` | `ok, assetId` | Import raw file into asset registry |

---

## Definition of Done

- [ ] Scene Tree items can be re-parented by drag & drop (native VSCode DnD)
- [ ] Asset Browser items can be dropped onto Scene Tree nodes to assign mesh/material
- [ ] Asset Browser items can be dropped onto the Viewport to place an asset in the world
- [ ] Viewport shows green-border feedback when an asset drag is in progress
- [ ] New files placed in `assets/` are auto-imported (FileSystemWatcher)
- [ ] Asset Browser has an "Import File..." button as fallback
- [ ] All IPC calls have engine-side handlers returning proper error responses
- [ ] DnD operations are undoable (integrate with undo-redo system)
- [ ] Unit tests for `DragDropCoordinator` state machine
- [ ] Integration tests for `scene.reparentNode` and `editor.placeAsset` IPC handlers
