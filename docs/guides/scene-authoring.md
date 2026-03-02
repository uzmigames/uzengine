# Scene Authoring Guide

This guide covers creating and editing `.uzscene` files — the plain-text scene format designed for human editing, version control, and AI generation.

---

## Overview

A `.uzscene` file represents a scene as a hierarchy of **nodes**. Each node has:
- A unique `id` and human-readable `name`
- A parent reference (0 = scene root)
- A type (Node3D, StaticMesh, Light, etc.)
- A set of properties (position, rotation, material, etc.)

### Why INI-like Format?

- **Diffable**: Git diffs show exactly what changed
- **AI-friendly**: LLMs can generate and edit scenes without GUI knowledge
- **Human-readable**: No XML/JSON nesting, straightforward key-value pairs
- **No GUIDs**: Stable integer IDs instead of opaque 128-bit identifiers
- **Versionable**: Perfect for game jam submissions, collaborative development

---

## File Structure

### Header

Every `.uzscene` file starts with a header section:

```ini
[uzscene version=1 name="MainLevel"]
```

| Field | Type | Purpose |
|-------|------|---------|
| `version` | integer | Format version (currently 1) |
| `name` | string | Scene display name (no spaces) |

### Node Blocks

Nodes are defined as INI sections:

```ini
[node id=1 type=Node3D name="Root" parent=0]
```

| Field | Type | Purpose |
|-------|------|---------|
| `id` | integer | Unique identifier (never reused) |
| `type` | enum | Node type (see Node Types below) |
| `name` | string | Human-readable name (no spaces) |
| `parent` | integer | Parent node ID (0 = scene root) |

### Properties

Properties follow the node header as key-value pairs:

```ini
[node id=2 type=StaticMesh name="Ground" parent=1]
mesh             = "res://models/terrain.glb"
material         = "res://materials/grass.uzmat"
cast_shadow      = true
receive_shadow   = true
tags             = ["world", "collision"]
```

Properties are **optional** — omitted properties use type defaults.

---

## Node Types

### Container Nodes

These nodes have no rendering or physics behavior. Use them to organize scenes.

| Type | Purpose |
|------|---------|
| `Node` | Generic container (no transform) |
| `Node3D` | 3D container with position/rotation/scale |

Example:

```ini
[node id=1 type=Node3D name="Root" parent=0]
position = [0.0, 0.0, 0.0]
rotation = [0.0, 0.0, 0.0]
scale    = [1.0, 1.0, 1.0]
```

### Mesh Nodes

Render meshes and materials.

| Type | Purpose | Key Properties |
|------|---------|-----------------|
| `StaticMesh` | Static renderable (immovable) | `mesh`, `material`, `cast_shadow`, `receive_shadow` |
| `SkinnedMesh` | Animated mesh (Phase 2) | `mesh`, `material`, `skeleton`, `animation_clip` |
| `Sprite3D` | 2D sprite in 3D space (Phase 2) | `texture`, `pixel_size`, `billboard` |

Example:

```ini
[node id=2 type=StaticMesh name="Player" parent=1]
mesh             = "res://models/player.glb"
material         = "res://materials/player.uzmat"
cast_shadow      = true
receive_shadow   = true
tags             = ["player"]
script           = "res://scripts/player.lua"
```

### Light Nodes

Emit light in the scene.

| Type | Purpose | Key Properties |
|------|---------|-----------------|
| `DirectionalLight` | Sun light (parallel) | `color`, `intensity`, `shadow_enabled`, `shadow_cascades` |
| `PointLight` | Omnidirectional light | `color`, `intensity`, `radius`, `shadow_enabled` |
| `SpotLight` | Cone-shaped light | `color`, `intensity`, `radius`, `angle_inner`, `angle_outer` |

Example:

```ini
[node id=3 type=DirectionalLight name="Sun" parent=1]
color            = [1.0, 0.96, 0.87]
intensity        = 1.5
shadow_enabled   = true
shadow_cascades  = 3
position         = [0.0, 50.0, 0.0]
rotation         = [-45.0, 30.0, 0.0]
```

### Camera Node

Renders the scene from a viewpoint.

| Property | Type | Default | Purpose |
|----------|------|---------|---------|
| `fov` | float | 75.0 | Field of view (degrees) |
| `near` | float | 0.1 | Near clipping plane (units) |
| `far` | float | 1000.0 | Far clipping plane (units) |
| `is_main` | bool | false | Make this the active camera |
| `projection` | enum | perspective | `perspective` or `orthographic` |

Example:

```ini
[node id=4 type=Camera name="MainCamera" parent=1]
fov      = 75.0
near     = 0.1
far      = 1000.0
is_main  = true
position = [0.0, 1.7, 5.0]
rotation = [0.0, 0.0, 0.0]
```

### Audio Node

Plays sound (Phase 3).

| Property | Type | Default | Purpose |
|----------|------|---------|---------|
| `clip` | path | (none) | Path to `.wav`/`.mp3`/`.ogg` file |
| `autoplay` | bool | false | Start playing on scene load |
| `loop` | bool | false | Repeat indefinitely |
| `volume` | float | 1.0 | 0.0 (silent) to 1.0 (max) |
| `spatial` | bool | true | 3D audio attenuation with distance |

Example:

```ini
[node id=5 type=AudioSource name="AmbientMusic" parent=1]
clip     = "res://audio/ambient.ogg"
autoplay = true
loop     = true
volume   = 0.5
spatial  = false
```

### Particle Emitter

GPU-driven particle system (Phase 3).

| Property | Type | Default | Purpose |
|----------|------|---------|---------|
| `effect` | path | (none) | Path to `.uzfx` particle effect |
| `autoplay` | bool | false | Start emitting on scene load |
| `loop` | bool | false | Continuous emission |

Example:

```ini
[node id=6 type=ParticleEmitter name="DustCloud" parent=1]
effect   = "res://effects/dust.uzfx"
autoplay = true
loop     = true
position = [0.0, 2.0, 0.0]
```

### UI Node

Game UI root (Phase 2).

| Property | Type | Default | Purpose |
|----------|------|---------|---------|
| `canvas_size` | array | [1920, 1080] | Base canvas resolution |
| `scale_mode` | enum | fit | `fit`, `fill`, or `stretch` |
| `stylesheet` | path | (none) | Path to `.css` file |

Example:

```ini
[node id=7 type=UIRoot name="HUD" parent=1]
canvas_size  = [1920, 1080]
scale_mode   = fit
stylesheet   = "res://ui/hud.css"
```

---

## Properties Reference

### Common Transform Properties

All Node3D-derived nodes have position, rotation, and scale:

```ini
position = [10.5, 2.3, -5.1]   ; X, Y, Z in world units
rotation = [45.0, 90.0, 0.0]   ; Euler angles (degrees), XYZ order
scale    = [1.0, 1.0, 2.0]     ; X, Y, Z scale factors
```

- **Default**: identity (position [0,0,0], rotation [0,0,0], scale [1,1,1])
- **Units**: world units; 1 unit = 1 meter in-game
- **Rotation order**: X (pitch), Y (yaw), Z (roll)

### Common Rendering Properties

```ini
cast_shadow      = true         ; Render into shadow maps
receive_shadow   = true         ; Sample shadow maps
lod_bias         = 0.0          ; LOD selection bias (-2.0 to 2.0)
visible          = true         ; Whether to render
layer            = "default"    ; Collision/rendering layer name
```

### Tags

Organize nodes with tag strings (max 64 unique tags per scene):

```ini
tags = ["enemy", "damageable", "ai"]
```

Use tags for:
- Gameplay queries: `scene.find_by_tag("pickup")`
- Rendering layers: Only render tagged nodes
- Physics grouping: Collide only with certain tags

### Script Attachment

Attach a Lua or C++ script to run per-node logic:

```ini
script = "res://scripts/player_controller.lua"
```

- **Lua scripts** (`.lua`): Loaded and executed per-frame
  - Called hooks: `on_start()`, `on_update(dt)`, `on_stop()`
  - Missing script: Node disabled with error logged, scene continues
- **C++ scripts** (`.cpp`): Compiled as behavior classes
  - Not yet supported (Phase 4+)

---

## Script Attachment Details

### Lua Script Lifecycle

When a node has `script = "res://scripts/foo.lua"`, the engine:

1. **Load**: Parse and compile the Lua file (or hot-reload if it changed)
2. **Create instance**: Create a new Lua table for this node
3. **Bind context**: Set `self` to reference the node's properties
4. **Call on_start()**: Run initialization logic
5. **Each frame**: Call `on_update(delta_time)` with elapsed time
6. **Cleanup**: Call `on_stop()` when scene unloads

### Script Error Handling

If a Lua script has a syntax error or crashes:
- **Behavior**: The node is disabled, errors logged to console
- **Scene continues**: Other nodes unaffected
- **Hot-reload**: Save the fixed script to re-enable

Example error in console:

```
[ERROR] Player: Lua script error: /game/player.lua:10: attempt to call nil
```

### Accessing Node Properties from Script

Inside a Lua script, `self` refers to the node:

```lua
-- player.lua
function on_start()
    print("Player spawned at", self.position)
    self.visible = true
end

function on_update(dt)
    self.position[1] = self.position[1] + dt * 10  -- Move +10 units/sec
end
```

---

## Physics Properties

Attach physics to nodes for collision and simulation (Phase 3).

### Rigid Body

```ini
[node id=10 type=RigidBody name="Box" parent=1]
mesh             = "res://models/cube.glb"
material         = "res://materials/wood.uzmat"
mass             = 5.0
linear_damping   = 0.1
angular_damping  = 0.1
gravity_enabled  = true
collider_shape   = "box"
collider_size    = [1.0, 1.0, 1.0]
```

### Static Body

```ini
[node id=11 type=StaticBody name="Floor" parent=1]
mesh             = "res://models/ground.glb"
material         = "res://materials/concrete.uzmat"
collider_shape   = "mesh"
```

### Trigger Volume

Non-physical collision detection:

```ini
[node id=12 type=TriggerVolume name="WinZone" parent=1]
collider_shape   = "box"
collider_size    = [5.0, 3.0, 5.0]
script           = "res://scripts/win_trigger.lua"
```

Inside the script:

```lua
-- win_trigger.lua
function on_collision_enter(other)
    print("Player entered win zone:", other.name)
    game_won = true
end
```

---

## Prefab Instantiation

Reuse scenes as prefabs by instantiating them:

```ini
[node id=20 type=Node3D name="EnemySpawner" parent=1]

[prefab id=21 ref="res://prefabs/goblin.uzscene" parent=20]
position = [0.0, 0.0, 0.0]

[prefab id=22 ref="res://prefabs/goblin.uzscene" parent=20]
position = [5.0, 0.0, 0.0]
```

### Prefab Override Blocks

Change properties of specific instances:

```ini
[prefab id=21 ref="res://prefabs/goblin.uzscene" parent=20]
position = [0.0, 0.0, 0.0]

[override id=21.3 type=StaticMesh name="Head"]
material = "res://materials/goblin_boss.uzmat"
```

The override applies to the node with id=3 inside the prefab instance (id 21.3).

---

## Tags System

### Defining and Querying Tags

Tags are lists of strings attached to nodes:

```ini
[node id=30 type=StaticMesh name="Pickup" parent=1]
mesh             = "res://models/coin.glb"
material         = "res://materials/gold.uzmat"
tags             = ["pickup", "gold", "collectible"]
script           = "res://scripts/pickup.lua"
```

From Lua, query nodes by tag:

```lua
-- pickup.lua
function on_collision_enter(other)
    local pickups = scene.find_by_tag("pickup")
    print("Found " .. #pickups .. " pickups in scene")
end
```

### Tag Limits

- **Max tags per node**: No limit
- **Max unique tag names per scene**: 64
- **Overhead**: 1 bitset per node (8 bytes)

---

## Working with the Editor

### Scene Tree Panel

The Scene Tree panel in VSCode shows the node hierarchy:

```
+ Root (Node3D)
  + Ground (StaticMesh)
  + Character (Node3D) [has script]
    + Head (StaticMesh)
    + Body (StaticMesh)
  + Sun (DirectionalLight)
  + MainCamera (Camera)
```

**Actions:**
- **Drag to reparent**: Drag a node onto another to make it a child
- **Right-click → Create Node**: Add a new child at that location
- **Right-click → Delete**: Remove node and children
- **Search**: Type node name to filter tree
- **Rename**: Double-click name to inline-edit

### Inspector Panel

The Inspector shows properties of the selected node:

```
Node: Player
─────────────────────
Type              | Node3D
Parent            | Root
Position          | [0.0, 1.7, 0.0]
Rotation          | [0.0, 0.0, 0.0]
Scale             | [1.0, 1.0, 1.0]
Script            | res://scripts/player.lua
Tags              | player, character
Visible           | ✓
Layer             | default
```

**Editing:**
- Click any value to edit
- Hover for tooltips on complex properties
- Press Enter to confirm, Escape to cancel
- Arrays show as `[x, y, z]` — edit as text

### 3D Gizmos

In the viewport:

- **W**: Translate mode (move along X/Y/Z axes)
- **E**: Rotate mode (rotate around X/Y/Z axes)
- **R**: Scale mode (scale on X/Y/Z axes)
- **X/Y/Z**: Constrain to single axis
- **Ctrl**: Grid snap (move by 0.5 unit increments)
- **Hold Shift**: Fine control (10× slower movement)

---

## Working with AI

UzEngine is designed for AI-generated content. Here's how to prompt Claude to create scenes:

### Basic Scene Generation

**Prompt:**
```
Create a .uzscene file for a simple game level with:
- A ground plane (StaticMesh)
- A player spawn point (Node3D with tag "spawn")
- A directional light (sun)
- A main camera
Use the res:// path convention. All meshes are at res://models/.
```

**Result:** Claude generates a complete, valid .uzscene file.

### With Physics and Scripting

**Prompt:**
```
Create a .uzscene for a breakable wooden crate:
- A RigidBody with a box collider
- Attach a Lua script at res://scripts/crate.lua
- The script should:
  - Spawn particles when hit
  - Destroy itself after 2 seconds if health < 0
- Use tags: ["destructible", "wood"]
```

### With Prefabs

**Prompt:**
```
Create a .uzscene that spawns a grid of enemies using prefab instantiation:
- Instantiate res://prefabs/skeleton.uzscene 3×3 (9 total)
- Space them 3 units apart
- Override the first one to use the "skeleton_boss" material
- Use tags to group them: ["enemy", "undead"]
```

### Prompt Best Practices

1. **Reference the spec**: Link to `docs/specs/scene-system.md` if asking for advanced features
2. **Show examples**: Provide a partial .uzscene as context
3. **Specify paths**: Tell Claude to use `res://models/`, `res://scripts/`, etc.
4. **Request validation**: Ask "Make sure the file is valid and parseable"

---

## Validation

### Common Errors

| Error | Cause | Fix |
|-------|-------|-----|
| `Duplicate node id=5` | Two nodes with same ID | Renumber one of them |
| `Parent node id=1 not found` | Parent ID doesn't exist | Use valid parent ID |
| `Unknown node type "Mesh"` | Typo in type name | Use correct type: `StaticMesh` |
| `Invalid color: [1.0, 2.0, 0.0]` | Color value > 1.0 | Clamp to 0.0-1.0 or use HDR values |
| `Script not found: res://bad.lua` | Lua path typo or missing file | Check file exists at path |

### Validation Tool

When editing in VSCode:

```
uzengine.validate-scene  ; Ctrl+Shift+V
```

Output:

```
✓ Scene is valid
✓ All parent IDs exist
✓ All scripts found
⚠ Warning: Node 5 has no material
```

### Checking Via IPC

From Claude or the debug console:

```json
{
    "command": "scene.validate",
    "file": "res://levels/main.uzscene"
}
```

Response:

```json
{
    "valid": true,
    "errors": [],
    "warnings": ["Node 5 has no material"]
}
```

---

## Complete Example Scene

A full, game-ready .uzscene file:

```ini
[uzscene version=1 name="Level01"]

; Scene root — organizational container
[node id=1 type=Node3D name="Root" parent=0]

; Environment
[node id=2 type=StaticMesh name="Ground" parent=1]
mesh             = "res://models/ground.glb"
material         = "res://materials/grass.uzmat"
cast_shadow      = true
receive_shadow   = true
tags             = ["world", "collision"]

; Player spawn point (invisible)
[node id=3 type=Node3D name="PlayerSpawn" parent=1]
position         = [0.0, 2.0, 0.0]
tags             = ["spawn"]

; Player character
[node id=4 type=Node3D name="Player" parent=1]
position         = [0.0, 2.0, 0.0]
tags             = ["player", "character"]

[node id=4.1 type=StaticMesh name="Body" parent=4]
mesh             = "res://models/player_body.glb"
material         = "res://materials/player.uzmat"
cast_shadow      = true
receive_shadow   = true

[node id=4.2 type=RigidBody name="Collider" parent=4]
mass             = 80.0
gravity_enabled  = true
linear_damping   = 0.1
angular_damping  = 0.05
collider_shape   = "capsule"
collider_size    = [0.3, 1.8]

[node id=4.3 type=AudioSource name="FootstepAudio" parent=4]
clip             = "res://audio/footsteps.wav"
spatial          = true
volume           = 0.7
script           = "res://scripts/player.lua"

; Enemies
[node id=5 type=Node3D name="Enemies" parent=1]

[prefab id=5.1 ref="res://prefabs/goblin.uzscene" parent=5]
position         = [10.0, 0.0, 0.0]

[prefab id=5.2 ref="res://prefabs/goblin.uzscene" parent=5]
position         = [15.0, 0.0, 0.0]

[override id=5.2.1 type=StaticMesh name="Goblin_Boss"]
material         = "res://materials/goblin_boss.uzmat"

; Lighting
[node id=10 type=DirectionalLight name="Sun" parent=1]
color            = [1.0, 0.96, 0.87]
intensity        = 1.2
shadow_enabled   = true
shadow_cascades  = 3
position         = [50.0, 100.0, 50.0]
rotation         = [-45.0, 30.0, 0.0]

[node id=11 type=PointLight name="TorchLight" parent=1]
color            = [1.0, 0.6, 0.3]
intensity        = 0.8
radius           = 10.0
position         = [5.0, 3.0, 0.0]
cast_shadow      = false

; Camera
[node id=20 type=Camera name="MainCamera" parent=4]
position         = [0.0, 0.5, -2.0]
rotation         = [0.0, 0.0, 0.0]
fov              = 75.0
near             = 0.1
far              = 1000.0
is_main          = true

; UI
[node id=30 type=UIRoot name="HUD" parent=1]
canvas_size      = [1920, 1080]
scale_mode       = fit
stylesheet       = "res://ui/hud.css"

; Audio listener (ear position for 3D audio)
[node id=40 type=AudioListener name="Listener" parent=4]
position         = [0.0, 0.2, 0.0]
```

This example demonstrates:
- Node hierarchy (parent/child relationships)
- Multiple node types (Mesh, Light, Camera, UI)
- Transform properties (position, rotation, scale)
- Physics (RigidBody, collider)
- Audio (3D spatial sound)
- Scripting (`script = ...`)
- Tagging (`tags = [...]`)
- Prefab instantiation with overrides
- Multiple light types with shadows

---

## Next Steps

- **Learn scripting**: See [lua-scripting-guide.md](lua-scripting-guide.md)
- **Add physics**: See [physics-guide.md](physics-guide.md)
- **Create assets**: See [asset-pipeline-guide.md](asset-pipeline-guide.md)
- **Editor workflow**: See [editor-workflow.md](editor-workflow.md)

