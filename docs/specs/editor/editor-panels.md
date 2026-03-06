# Editor Panels Spec

## Overview

All editor UI lives inside VSCode as extension panels. There is no separate editor window. The engine process (`render.exe --editor`) handles scene state and rendering; the VSCode extension handles all user interaction.

---

## 1. Viewport Panel

**VSCode API**: `WebviewPanel`

The viewport streams JPEG frames from the engine via WebSocket and renders them on a `<canvas>`. It also handles mouse/keyboard input for camera navigation and sends input events back to the engine via WebSocket.

### Layout

```
┌──────────────────────────────────────────────────┐
│  [3D] [2D] [Asset Preview]          FPS: 60      │  ← toolbar
├──────────────────────────────────────────────────┤
│                                                  │
│                  3D VIEWPORT                     │
│            (canvas, full area)                   │
│                                                  │
│  W/A/S/D + right-click = fly camera             │
│  Middle-click drag = orbit                       │
│  Scroll = zoom                                   │
├──────────────────────────────────────────────────┤
│  Gizmo: [Move] [Rotate] [Scale]   [Local/World]  │  ← bottom bar
└──────────────────────────────────────────────────┘
```

### Camera Navigation

| Input | Action |
|-------|--------|
| Right-click + WASD | Fly camera |
| Right-click + QE | Up/down |
| Middle-click drag | Orbit around selection |
| Scroll wheel | Zoom / fly speed |
| F | Frame selected node |
| Alt+click | Orbit around clicked point |

### IPC — Input Events to Engine

```json
{ "fn": "editor.cameraMove", "args": { "delta": [0.1, 0, 0], "yaw": 0.01, "pitch": 0.0 } }
{ "fn": "editor.pick",       "args": { "x": 512, "y": 384 } }  → returns node id
{ "fn": "editor.setGizmo",   "args": { "mode": "move" } }
```

### Node Picking

When the user clicks on the viewport, the extension sends a `editor.pick` call with the pixel coordinates. The engine performs a GPU pick pass (renders node IDs to an offscreen buffer) and returns the node ID. The extension then selects that node in the Scene Tree and Inspector.

---

## 2. Scene Tree Panel

**VSCode API**: `TreeDataProvider` registered to the `uzengine.sceneTree` view

Native VSCode tree widget — no webview, no HTML. Displays the node hierarchy, supports selection, rename, drag-and-drop re-parenting, and context menus.

### Tree Item Display

```
▼ 🌐 Level1                     [uzscene]
  ▼ 📦 World                    [Node3D]
    ▼ 🎯 Ground                 [StaticMesh]
        mesh: terrain.glb
    💡 Sun                      [DirectionalLight]
    ▼ 📦 Buildings              [Node3D]
        🎯 House1               [StaticMesh]
        🎯 House2               [StaticMesh]
  ▼ 🎮 Player                   [Node3D]
      🔘 Collision              [CapsuleCollider]
      🎯 Body                   [StaticMesh]
      📷 MainCamera             [Camera]
```

### Context Menu Actions

- **Add Child Node** (opens node type picker)
- **Duplicate**
- **Delete**
- **Rename** (inline edit)
- **Move to** (re-parent via drag or submenu)
- **Select in Viewport** (focuses viewport on this node)
- **Copy / Paste** (copies `.uzscene` fragment to clipboard)

### IPC Sync

The extension maintains a local copy of the scene tree (fetched via `scene.getTree()`). When the engine signals a change (node added/removed/renamed), the extension refreshes the tree view. Edits in the tree (rename, re-parent) are sent back via IPC.

---

## 3. Inspector Panel

**VSCode API**: `WebviewPanel` using [Webview UI Toolkit](https://github.com/microsoft/vscode-webview-ui-toolkit)

Shows properties of the currently selected node. Grouped by category. Live editing — each change is immediately sent to the engine via `scene.setProperty`.

### Layout Example (DirectionalLight selected)

```
Inspector — Sun [DirectionalLight]
─────────────────────────────────────
▼ Node
  Name         [Sun           ]
  Type         DirectionalLight (read-only)
  ID           3 (read-only)

▼ Transform
  Position     X [0.0 ] Y [50.0] Z [0.0 ]
  Rotation     X [-45.] Y [30.0] Z [0.0 ]
  Scale        X [1.0 ] Y [1.0 ] Z [1.0 ]

▼ Light
  Color        [■ #FFF5E0          ]
  Intensity    [━━━━━━━━━●━━] 1.5
  Cast Shadow  [✓]
  Cascades     [▼ 3               ]
─────────────────────────────────────
```

### Property Widget Types

| Property type | Widget |
|--------------|--------|
| `float` | Number input + slider (with min/max hints) |
| `int` | Number input |
| `bool` | Checkbox |
| `string` | Text input |
| `vec3` | Three number inputs (X Y Z) |
| `color` | Color picker |
| `enum` | Dropdown |
| `resource_path` | Text input + file picker button |
| `transform` | Expandable section (position, rotation, scale rows) |

### IPC Flow

```
User edits "intensity" field → 1.5
    ↓
Extension sends:
{ "fn": "scene.setProperty", "args": { "id": 3, "key": "intensity", "value": 1.5 } }
    ↓
Engine applies change → re-render
    ↓
Inspector stays in sync (no full refresh needed for single property)
```

---

## 4. Asset Browser Panel

**VSCode API**: `TreeDataProvider` registered to `uzengine.assets` view

Displays the project file tree filtered by asset type. Supports drag-and-drop from the asset browser to the viewport or inspector (to assign a mesh/material/texture).

### Tree Structure

```
▼ 📁 Assets
  ▼ 📁 meshes
      🎯 terrain.glb
      🎯 player.glb
      🎯 house.glb
  ▼ 📁 textures
      🖼 wood_albedo.png
      🖼 wood_normal.png
  ▼ 📁 materials
      ⚙ grass.uzmat
      ⚙ wood.uzmat
  ▼ 📁 shaders
      📄 pbr.hlsl
      📄 cel.hlsl
  ▼ 📁 scenes
      🌐 main.uzscene
      🌐 player.uzscene
```

### Context Menu Actions

- **Open** (opens file in text editor for HLSL/UZSCENE/UZMAT)
- **Reimport** (re-runs import pipeline for meshes/textures)
- **Show in Explorer**
- **Delete**
- **New Material** (creates `.uzmat` from template)
- **New Scene** (creates `.uzscene` from template)
- **New Shader** (creates `.hlsl` from template)

---

## 5. Project Settings Panel

**VSCode API**: `WebviewPanel` (full webview)

Edits `project.uzproj` (TOML). Organized into tabs.

```toml
# project.uzproj
[project]
name    = "MyGame"
version = "0.1.0"
main_scene = "res://scenes/main.uzscene"

[renderer]
resolution   = [1280, 720]
vsync        = true
msaa         = 4
shadow_atlas = 2048

[physics]
gravity = [0, -9.81, 0]
enabled = true

[audio]
master_volume  = 1.0
sample_rate    = 44100
```

### Tabs

1. **General** — project name, version, main scene, author
2. **Renderer** — resolution, MSAA, shadow quality, tonemapping
3. **Physics** — gravity, simulation rate, enabled
4. **Audio** — volumes, sample rate
5. **Input** — key bindings (Phase 3)
6. **Export** — build settings (Phase 4)

---

## 6. Play Controls (Status Bar)

**VSCode API**: `StatusBarItem`

```
[  ▶ Play  ]  [  ⏸ Pause  ]  [  ■ Stop  ]    Scene: main.uzscene    FPS: 60
```

States:
- **Idle** (no engine running): shows ▶ Play only
- **Editor mode** (engine running, editing): shows ▶ Play
- **Play mode** (game running): shows ⏸ Pause + ■ Stop
- **Paused**: shows ▶ Resume + ■ Stop

### Play Flow

```
Click ▶ Play
    ↓
Extension checks for unsaved changes → auto-save or prompt
    ↓
Extension spawns: render.exe --play --project=./ --scene=main.uzscene
    ↓
Viewport switches to play process stream
    ↓
Status bar shows Pause + Stop
    ↓
Click ■ Stop → kill play process → editor mode resumes
```

---

## 7. Console / Output Panel

**VSCode API**: `OutputChannel` named "UzEngine"

Receives log lines from the engine via IPC:
```json
{ "fn": "log.append", "args": { "level": "warn", "msg": "Texture not found: grass.png" } }
```

Log levels: `debug` (grey), `info` (white), `warn` (yellow), `error` (red)

---

## 8. Shader Editor Integration

The shader editor is not a custom panel — it's the standard VSCode text editor with:

1. **HLSL syntax highlighting** — via "HLSL Tools" VSCode extension
2. **Compilation errors as diagnostics** — extension creates a `DiagnosticCollection` and populates it from `shader.getErrors` IPC response
3. **Save → hot-reload** — `onDidSaveTextDocument` handler calls `shader.reload` when a `.hlsl` or `.glsl` file is saved
4. **Status bar item** — shows shader compilation status

```
Shader: pbr.hlsl  [✓ Compiled 42ms ago]      ← status bar on active HLSL file
Shader: pbr.hlsl  [✗ 2 errors]               ← on compile error
```

---

## Panel Layout in VSCode

```
┌────────────────────────────────────────────────────────────┐
│  Activity Bar      │  Primary Sidebar    │  Editor Group   │
│                    │                     │                 │
│  🎮 UzEngine ◄     │  ▼ Scene Tree       │  ┌───────────┐ │
│                    │    ▼ World          │  │           │ │
│                    │      Ground        │  │  VIEWPORT │ │
│                    │      Sun           │  │           │ │
│                    │    ▼ Player        │  │  (3D view)│ │
│                    │      Camera        │  │           │ │
│                    ├─────────────────── │  └───────────┘ │
│                    │  ▼ Assets          │  ┌───────────┐ │
│                    │    meshes/         │  │ INSPECTOR │ │
│                    │    textures/       │  │ Sun       │ │
│                    │    materials/      │  │ intensity │ │
│                    │    shaders/        │  │ 1.5 ━●━   │ │
│                    │                   │  └───────────┘ │
├────────────────────────────────────────────────────────────┤
│  Panel (bottom)                                            │
│  [Console] [Terminal] [UzEngine Output]                    │
├────────────────────────────────────────────────────────────┤
│  Status Bar  ▶ Play  ■ Stop    Scene: main.uzscene  FPS:60 │
└────────────────────────────────────────────────────────────┘
```
