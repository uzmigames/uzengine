# Editor Workflow Guide

This guide covers using the UzEngine editor — a VSCode extension paired with a separate engine process for live editing and playtesting.

---

## Overview

The UzEngine editor is **VSCode + UzEngine.exe** running as separate processes communicating via IPC:

- **VSCode**: UI panels (Scene Tree, Inspector, Asset Browser, Play Controls)
- **UzEngine.exe**: Renders the viewport, handles physics/scripts, responds to edits
- **IPC Bridge**: JSON-based command/response communication

This architecture allows **live editing** — change a property, see it update in real-time.

---

## Starting the Editor

### Prerequisites

1. **Build the project** (see [build.md](build.md))
2. **VSCode installed** with the UzEngine extension
3. **Project open** in VSCode (File → Open Folder → `e:\UzEngine`)

### Launch Editor Mode

Press **Ctrl+Shift+B** (Run Build Task) or use the command palette:

```
Ctrl+Shift+P → UzEngine: Start Editor
```

This launches:
- `UzEngine.exe --editor` (separate process, spawned by extension)
- VSCode panels appear automatically
- Viewport initializes with first scene in project

### Manual Launch (Advanced)

From PowerShell:

```powershell
$env:UZENGINE_PROJECT = "e:\UzEngine"
.\build\Release\UzEngine.exe --editor
```

Or from VSCode integrated terminal:

```bash
./build/Release/UzEngine.exe --editor
```

---

## Project Structure

A typical UzEngine project:

```
project/
├── project.uzproj            ; Project manifest (TOML)
├── scenes/
│   ├── main.uzscene          ; Main level
│   └── menu.uzscene          ; Menu scene
├── prefabs/
│   ├── enemy.uzscene         ; Prefab: goblin
│   └── pickup.uzscene        ; Prefab: collectible
├── scripts/
│   ├── player.lua            ; Player controller
│   ├── enemy_ai.lua          ; Enemy behavior
│   └── game_manager.lua      ; Global game logic
├── shaders/
│   ├── materials/
│   │   └── pbr.hlsl
│   └── include/
│       └── common.hlsli
├── materials/
│   ├── grass.uzmat           ; Material (references shader)
│   └── player.uzmat
├── models/
│   ├── terrain.glb           ; Imported mesh
│   └── character.fbx
├── audio/
│   ├── ambient.ogg
│   └── footsteps.wav
├── textures/
│   ├── grass_albedo.png
│   └── grass_normal.png
└── ui/
    ├── hud.css               ; HUD stylesheet
    └── menu.html             ; Menu layout
```

### Asset Paths

All asset references use the `res://` prefix:

```
res://scenes/main.uzscene
res://scripts/player.lua
res://models/terrain.glb
res://textures/grass_albedo.png
res://shaders/materials/pbr.hlsl
```

The `res://` root resolves to the project directory.

### Project Manifest

File: `project.uzproj` (TOML format)

```toml
[project]
name = "MyGame"
version = "0.1.0"
author = "Your Name"

[build]
target_scene = "res://scenes/main.uzscene"
output_dir = "build/game"

[engine]
window_width = 1280
window_height = 720
vsync = true
debug_mode = true
```

---

## Viewport Controls

### Mouse and Keyboard

| Input | Action |
|-------|--------|
| **Middle Mouse Drag** | Orbit camera (rotate around focus point) |
| **Middle Mouse Wheel** | Zoom in/out |
| **Right Mouse Drag** | Pan camera (translate view) |
| **W** | Translate gizmo |
| **E** | Rotate gizmo |
| **R** | Scale gizmo |
| **X/Y/Z** | Constrain gizmo to single axis |
| **Ctrl** | Grid snap (move in 0.5-unit increments) |
| **Shift** | Fine control (10× slower movement) |
| **F** | Frame selection (focus camera on selected node) |
| **V** | Toggle viewport shading (wireframe, lit, etc.) |

### Viewport Shading Modes

Cycle with **V**:

1. **Lit** — Standard shaded view with lights
2. **Unlit** — Flat colors, no shadows
3. **Wireframe** — Triangle edges only
4. **Depth** — Depth visualization
5. **Normal** — Surface normals

---

## Scene Tree Panel

The **Scene Tree** shows the hierarchy of all nodes in the current scene.

```
+ Root (Node3D)
  + Ground (StaticMesh)
  + Character (Node3D)
    + Head (StaticMesh)
    + Body (StaticMesh)
    + Collider (RigidBody)
  + Sun (DirectionalLight)
  + MainCamera (Camera)
  + HUD (UIRoot)
```

### Scene Tree Actions

#### Creating Nodes

Right-click in the tree → **Create Node**:

```
Create Node
├── Node (empty container)
├── Node3D (3D container with transform)
├── StaticMesh (renderable mesh)
├── Camera (viewport)
├── DirectionalLight
├── PointLight
├── SpotLight
├── AudioSource
├── ParticleEmitter
└── UIRoot
```

Select a parent first, then create → new node becomes a child.

#### Reparenting

Drag a node onto another to change its parent:

```
Before:           After:
+ Root            + Root
  + Enemy         + Enemy
  + Item            + Head
                   + Item
```

The visual hierarchy updates immediately in the viewport.

#### Deleting Nodes

Right-click → **Delete** (or press Delete key):

```
Confirm deletion:
Delete "Enemy" and 5 children?

[ Cancel ]  [ Delete ]
```

All children are deleted too. This action can be **undone** (Ctrl+Z).

#### Renaming

Double-click the node name to inline-edit:

```
Enemy [editing...]
```

Press **Enter** to confirm, **Escape** to cancel.

#### Search

Type in the search box at the top to filter the tree:

```
Search: enemy
────────────────────
+ Root
  + Enemy_01
  + Enemy_02
  + Enemy_Spawner
```

Clear the search to show full tree again.

### Multi-Select in Scene Tree

- **Ctrl+Click**: Add to selection
- **Shift+Click**: Range select
- **Ctrl+A**: Select all nodes
- **Click empty space**: Clear selection

---

## Inspector Panel

The **Inspector** shows properties of the selected node(s).

### Property Types

| Type | Example | Editing |
|------|---------|---------|
| **Text** | `name = "Player"` | Click to edit, Enter to confirm |
| **Number** | `mass = 5.0` | Drag to adjust, or click to type |
| **Boolean** | `visible = ✓` | Click to toggle |
| **Enum** | `projection = perspective` | Dropdown menu |
| **Vector3** | `position = [1.5, 2.0, -3.0]` | Click each component, or paste `[x, y, z]` |
| **Color** | `color = [1.0, 0.5, 0.0]` | Color picker |
| **Path** | `script = res://scripts/...` | Browse dialog or paste path |

### Inspector Features

#### Reset to Default

Right-click a property → **Reset to Default**:

```
Reset "position" to [0.0, 0.0, 0.0]
```

#### Multi-Select (Shared Properties)

Select multiple nodes with Ctrl+Click. Inspector shows **only shared properties**:

```
Selected: 3 nodes
─────────────────
Position     | [varies]    (not shown if different)
Visible      | ✓
Layer        | default
Material     | (empty)     (property not on all selected)
```

Edit a shared property to change all selected nodes at once.

#### Undo/Redo

Every property edit creates an undo point (Ctrl+Z / Ctrl+Shift+Z).

History limit: 100 actions (oldest are discarded).

---

## Asset Browser Panel

The **Asset Browser** shows importable assets (.fbx, .png, .ogg, etc.).

### Browser View

```
res://
├── models/
│   ├── terrain.glb           [✓ Imported]
│   ├── character.fbx         [⚠ Import settings]
│   └── particle.obj          [✗ Unsupported]
├── textures/
│   ├── grass_albedo.png      [✓ Imported]
│   └── grass_normal.png      [✓ Imported]
├── audio/
│   ├── ambient.ogg           [✓ Imported]
│   └── footsteps.wav         [✓ Imported]
└── shaders/
    └── materials/
        └── pbr.hlsl          [✓ Shader]
```

**Badges:**
- **✓ Imported**: Ready to use
- **⚠ Settings**: Configure import, then import
- **✗ Unsupported**: Format not recognized

### Importing Assets

1. **Place source file** in project (e.g., drag `.fbx` into folder)
2. **Asset Browser auto-detects** (watches for new files)
3. **Right-click** → **Import** (or auto-import if enabled)
4. **Set import options** (mesh LODs, texture compression, etc.)
5. **Click Import** → generates `.import.json` sidecar
6. **Asset ready** to use in scenes

### Dragging to Scene

From Asset Browser, drag an asset into the viewport:

- **Mesh** (`.glb`, `.fbx`): Creates **StaticMesh** node
- **Texture** (`.png`, `.jpg`): Creates **Sprite3D** or updates material
- **Audio** (`.wav`, `.ogg`): Creates **AudioSource** node
- **Prefab** (`.uzscene`): Creates **prefab instance** node

---

## Undo/Redo

All edits are undoable:

| Action | Result |
|--------|--------|
| Ctrl+Z | Undo last action |
| Ctrl+Shift+Z | Redo next action |

**Undoable actions:**
- Property edits
- Node creation
- Node deletion
- Reparenting
- Gizmo transforms
- Script saves (hot-reload)

**History limit:** 100 actions; oldest discarded when limit reached.

---

## 3D Gizmos

Use keyboard shortcuts to activate different editing modes.

### Translate (W)

Move nodes along axes:

```
    Y
    |
    *--X
   /
  Z
```

- **Click axis line**: Constrain to that axis
- **Click center cube**: Free XYZ movement
- **Ctrl+drag**: Grid snap (0.5 unit increments)
- **Shift+drag**: Fine control (10× slower)

### Rotate (E)

Rotate around axes:

```
    Ring for free rotation
   /       |       \
  /    Y  |  X    \
 Z                 R
```

- **Click ring color**: Constrain to that axis
- **Click center circle**: Free rotation
- **Shift+drag**: Fine control

### Scale (R)

Resize objects:

```
  [--+--]  X
   \ | /
    \|/    Y
    -+-    (center = uniform scale)
    /|\
   / | \   Z
```

- **Click axis line**: Scale on that axis
- **Click center cube**: Uniform scale all axes
- **Shift+drag**: Fine control

### Grid Snap

Hold **Ctrl** while dragging to snap to grid:

- **Position**: Snap to 0.5 unit increments
- **Rotation**: Snap to 15 degree increments
- **Scale**: Snap to 0.1 factor increments

---

## Play Mode

Test gameplay by running the scene.

### Starting Play Mode

Press **F5** (or Ctrl+Shift+P → UzEngine: Play):

```
[PLAYING] 00:12.5s   |  Esc to stop
─────────────────────
FPS: 60.0
Entities: 42
Memory: 128 MB
```

A **separate process** (`UzEngine.exe --play`) is launched:
- Fully isolated from editor
- No UI overlays (pure game experience)
- Reads same scene file
- Scripts run normally

### Stopping Play Mode

Press **Esc** (or click **Stop** button in panel):

```
Play mode stopped.
Session: 00:12.5s
Entities alive at end: 42
```

Returns to editor view with all edits preserved.

### Play Mode Limitations

- **No live editing**: Changes in editor are not reflected in running game
- **Separate state**: Game state doesn't merge back to editor
- **Console logs**: Visible in VSCode output panel (select "UzEngine" channel)

### Performance Profiling

In Play Mode, profiler data appears in output:

```
Frame time: 16.5 ms
  CPU: 8.2 ms
  GPU: 8.3 ms
  Overhead: 0.1 ms

Top systems:
  Physics: 3.2 ms
  Rendering: 5.1 ms
  Scripts: 2.5 ms
```

Use **F11** (in-game) to toggle detailed profiler overlay.

---

## Prefab Editing

Reusable scene templates are edited in a special **Prefab Isolation Mode**.

### Entering Prefab Mode

Double-click a `.uzscene` prefab in Asset Browser:

```
res://prefabs/goblin.uzscene [Double-click]

→ Viewport enters prefab mode
→ Scene Tree shows only prefab contents
→ Breadcrumb shows: Scenes > Prefabs > goblin.uzscene
```

### Prefab Isolation

Inside prefab mode, you're editing the **template**, not instances:

```
Before (instances):           After (template):
+ Root                        + GoblinRoot (Node3D)
  + Goblin_001                  + Body (StaticMesh)
  + Goblin_002                  + Head (StaticMesh)
  + Goblin_003                  + Weapon (StaticMesh)
                                + Collider (RigidBody)
```

All instances automatically **inherit** changes to the template.

### Override Panel

Instances can **override** individual properties:

```
Instances of goblin.uzscene: 3
─────────────────────────────
Position        | [varies]
Material        | ▣ (1 override)
                |   Instance: goblin_002
                |   Value: goblin_boss.uzmat
```

Blue highlight shows which instances override this property. Click to edit or clear.

### Saving and Closing Prefab

When done editing:

1. **File → Save** (Ctrl+S) saves the prefab template
2. **Breadcrumb → Scenes** returns to main scene
3. All instances **automatically reload** with new template

---

## Shader Hot-Reload

Edit HLSL shaders and see changes instantly.

### Edit Workflow

1. **Open shader** in VSCode (e.g., `res://shaders/materials/pbr.hlsl`)
2. **Make changes** (edit code)
3. **Save** (Ctrl+S)
4. **DXC compiler** runs automatically (~50 ms)
5. **Viewport updates** with new shader
6. **Errors appear** as red diagnostics if compilation fails

### Compilation Errors

Errors appear inline with line numbers:

```
pbr.hlsl
  Line 45: error X3004: undeclared identifier 'AlbedoSampler'
  Line 120: warning X3571: implicit cast from 'float4' to 'float3'
```

Also logged to Output → Engine:

```
[ERROR] Shader compilation failed: res://shaders/materials/pbr.hlsl
  Line 45: undeclared identifier 'AlbedoSampler'
```

**Fix the error** → save again → viewport updates.

### Multi-Variant Compilation

If the shader has variants (from `.uzmat` config):

```
[variants]
HAS_ALBEDO_TEX = true
HAS_NORMAL_TEX = true
SKINNED = true
```

The compiler generates **all combinations**:

```
Compiling: pbr.hlsl
  Variant 1/4: HAS_ALBEDO_TEX=1, HAS_NORMAL_TEX=1, SKINNED=0 ✓
  Variant 2/4: HAS_ALBEDO_TEX=1, HAS_NORMAL_TEX=1, SKINNED=1 ✓
  Variant 3/4: HAS_ALBEDO_TEX=0, HAS_NORMAL_TEX=1, SKINNED=0 ✓
  Variant 4/4: HAS_ALBEDO_TEX=0, HAS_NORMAL_TEX=1, SKINNED=1 ✓
All variants compiled in 152 ms
```

Each variant is a separate **Pipeline State Object (PSO)** cached for instant rendering.

---

## Lua Hot-Reload

Edit Lua scripts and see behavior changes within ~60ms.

### Script Edit Workflow

1. **Open Lua script** (e.g., `res://scripts/player.lua`)
2. **Make changes** (edit code)
3. **Save** (Ctrl+S)
4. **Lua VM reloads** automatically (~60 ms)
5. **Behavior updates** next frame
6. **Errors logged** to console if script crashes

### Script Reload Behavior

When a script is modified:

- **on_stop()** called on old instance (cleanup)
- **Script reloaded** from disk
- **on_start()** called on new instance (re-init)
- **State preserved** via `ctx.saves` if needed

Example script with hot-reload-aware state:

```lua
-- player.lua

function on_start()
    self.health = ctx.saves:get("player_health") or 100
    self.position = ctx.saves:get("player_pos") or [0, 1, 0]
end

function on_update(dt)
    ; Move player
    self.position[1] = self.position[1] + input.get_axis("horizontal") * dt * 5.0

    ; Save state for next reload
    ctx.saves:set("player_health", self.health)
    ctx.saves:set("player_pos", self.position)
end

function on_stop()
    ; Cleanup (called before reload or scene unload)
    print("Player script unloading...")
end
```

### Debugging Lua Errors

If a script crashes:

1. **Error logged** to console:
   ```
   [ERROR] Player: Lua script error: /scripts/player.lua:45: attempt to call nil
   ```

2. **Node is disabled** to prevent repeated crashes

3. **Fix the error** in VSCode

4. **Save again** to trigger reload

5. **Node re-enabled** when script reloads successfully

---

## Debug Console

Access the in-game debug console (even while editing):

### Opening Console

Press **~** (tilde) in the viewport:

```
> █

Available commands:
  scene.print_tree
  physics.debug_draw_toggle
  console.clear
```

### Built-in Commands

| Command | Purpose |
|---------|---------|
| `scene.print_tree` | Print scene hierarchy to console |
| `scene.find entity_type` | Find nodes by type |
| `physics.debug_draw_toggle` | Toggle physics wireframes |
| `physics.set_gravity [x y z]` | Override gravity vector |
| `console.clear` | Clear console output |
| `console.history` | Show previous commands |
| `help` | List all commands |

### Custom Commands (Lua)

Register commands from Lua scripts:

```lua
-- Register a command
ctx.console:register_command("heal_player", function(args)
    self.health = 100
    print("Player healed!")
end)
```

Then in console:

```
> heal_player
Player healed!
```

---

## Common Workflows

### Spawning an Enemy

1. **Create → Node3D** (name: "Enemy")
2. **Create → StaticMesh** (child of Enemy, mesh: `res://models/goblin.glb`)
3. **Create → RigidBody** (child of Enemy for physics)
4. **Inspector** → Set script: `res://scripts/enemy_ai.lua`
5. **Inspector** → Add tag: "enemy"
6. **Position** in viewport (W gizmo)
7. **F5** to test in play mode

### Adding a Trigger Zone

1. **Create → TriggerVolume** (name: "WinZone")
2. **Inspector** → Set collider shape: "box"
3. **Inspector** → Set size: `[5, 3, 5]`
4. **Position** in scene (where player should win)
5. **Attach script** with collision handler:

```lua
-- win_trigger.lua
function on_collision_enter(other)
    if other.name == "Player" then
        print("You won!")
        game_won = true
    end
end
```

### Creating a HUD

1. **Create → UIRoot** (name: "HUD")
2. **Inspector** → Set stylesheet: `res://ui/hud.css`
3. **Create → UIElement** children for health bar, score, etc.
4. **Edit CSS** in `res://ui/hud.css` to position and style
5. **Attach Lua script** for dynamic updates:

```lua
-- hud.lua
function on_update(dt)
    local health_bar = scene.find_node("HealthBar")
    if health_bar then
        health_bar.width = player.health / 100.0  ; 0-1 scale
    end
end
```

### Debugging Physics Issues

1. **In viewport** → Press **V** to cycle viewport modes
2. **Select Physics wireframe** mode
3. **Physics debug draw** shows all rigid bodies and colliders
4. **Check collider shapes** (green=static, blue=dynamic)
5. **Red** indicates collision contacts

Or via console:

```
> physics.debug_draw_toggle
Physics debug draw: enabled
```

---

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| **W** | Translate gizmo |
| **E** | Rotate gizmo |
| **R** | Scale gizmo |
| **X/Y/Z** | Constrain gizmo axis |
| **Ctrl** | Grid snap |
| **Shift** | Fine control |
| **F** | Frame selected node |
| **V** | Cycle viewport shading |
| **~** | Open debug console |
| **F5** | Start play mode |
| **Esc** | Stop play mode |
| **Ctrl+Z** | Undo |
| **Ctrl+Shift+Z** | Redo |
| **Ctrl+S** | Save scene |
| **Ctrl+D** | Duplicate selected node |
| **Delete** | Delete selected node |
| **Ctrl+A** | Select all nodes |
| **Ctrl+Shift+B** | Build/start editor |

---

## Next Steps

- **Scene authoring**: See [scene-authoring.md](scene-authoring.md)
- **Scripting**: See [lua-scripting-guide.md](../guides/lua-scripting-guide.md)
- **Shaders**: See [shader-authoring.md](shader-authoring.md)
- **Assets**: See [asset-pipeline-guide.md](../guides/asset-pipeline-guide.md)

