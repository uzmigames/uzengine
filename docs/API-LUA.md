# Lua API Reference — UzEngine

Complete API reference for the Lua scripting system in UzEngine. This documents the **current, working implementation** of Lua bindings.

> **Status**: Phase 1 implementation. Core bindings for scene control and camera manipulation are available.

---

## Overview

The Lua scripting system provides a sandboxed LuaJIT 2.1 environment where each node can execute a `.lua` script. Scripts have three lifecycle functions:

- `on_start()` — called once when the node/script initializes
- `on_update(dt)` — called every frame (dt = delta time in seconds)
- `on_stop()` — called when the node/script shuts down

Each script gets its own isolated Lua environment (registry reference) with access to a global `NODE_ID` constant identifying its parent node.

---

## Script Lifecycle

### Loading a Script

In a `.uzscene` file, attach a script to a node:

```ini
[node "MyActor"]
type = StaticMesh
script = assets/scripts/example.lua
```

When the scene loads:
1. Engine creates a `LuaScriptInstance` for this node
2. Engine loads and compiles `assets/scripts/example.lua`
3. If `on_start()` function exists, it is called
4. Every frame, `on_update(dt)` is called (if defined)
5. On shutdown, `on_stop()` is called (if defined)

### Global NODE_ID

Each script has access to a global constant:

```lua
function on_update(dt)
    local x, y, z = uz.scene.get_rotation(NODE_ID)
    -- NODE_ID is the unique identifier for this node
end
```

### Required Functions

Define only the functions you need:

```lua
-- Minimal script (just on_update)
function on_update(dt)
    print("Frame " .. NODE_ID)
end
```

```lua
-- Full lifecycle
function on_start()
    print("Node started")
end

function on_update(dt)
    print("Frame " .. dt)
end

function on_stop()
    print("Node stopped")
end
```

---

## Core API

### Global Utilities

#### `uz.log(message)`

Print a message to the engine log with a newline.

```lua
uz.log("Hello from Lua!")
uz.log("Position: " .. tostring(x))
```

Output appears in the engine console and VSCode output channel.

---

#### `uz.delta_time()`

Returns the frame delta time in seconds (float).

```lua
function on_update(dt)
    local actual_dt = uz.delta_time()
    assert(dt == actual_dt)  -- Same as parameter
end
```

---

#### `uz.total_time()`

Returns the total elapsed time since scene load in seconds (float).

```lua
function on_update(dt)
    local t = uz.total_time()
    local angle = t * 2.0  -- Rotate 2 radians per second
end
```

---

### Scene API (`uz.scene.*`)

#### `uz.scene.get_rotation(node_id)`

Returns the rotation of a node in degrees (rx, ry, rz).

**Returns**: three floats (rx, ry, rz)

```lua
local rx, ry, rz = uz.scene.get_rotation(NODE_ID)
uz.log("Rotation: " .. rx .. ", " .. ry .. ", " .. rz)
```

---

#### `uz.scene.set_rotation(node_id, rx, ry, rz)`

Sets the rotation of a node in degrees.

**Parameters**:
- `node_id` — node identifier
- `rx, ry, rz` — rotation around X, Y, Z axes in degrees

```lua
uz.scene.set_rotation(NODE_ID, 0, 45, 0)  -- 45° around Y axis
```

---

### Camera API (`uz.camera.*`)

#### `uz.camera.set_position(x, y, z)`

Sets the editor camera position.

**Parameters**:
- `x, y, z` — world position coordinates (floats)

```lua
uz.camera.set_position(10, 5, 20)
```

---

#### `uz.camera.look_at(x, y, z)`

Points the editor camera at a target position.

**Parameters**:
- `x, y, z` — target position in world space

```lua
uz.camera.look_at(0, 2, 0)  -- Look at point (0, 2, 0)
```

---

## Sandboxing

The Lua VM is sandboxed to prevent dangerous operations. The following globals are **disabled**:

- `os.execute()` — cannot run system commands
- `io.open()`, `io.read()` — cannot access files
- `loadfile()`, `load()` — cannot dynamically load code
- `debug.* ` — no debug library access
- `require()` — no `require()`-based module system (Phase 2+)

### Available Globals

The following standard Lua libraries are **enabled**:

- `math.*` — all math functions (sin, cos, sqrt, etc.)
- `string.*` — string manipulation (sub, format, etc.)
- `table.*` — table operations (insert, remove, concat, etc.)
- `math.random()`, `math.randomseed()`
- `type()`, `tonumber()`, `tostring()`, `pairs()`, `ipairs()`
- `assert()`, `error()`, `pcall()`
- `print()` — outputs to Lua stdout (same as `uz.log()`)

---

## Example Scripts

### Rotate Node Every Frame

**File**: `assets/scripts/rotate_node.lua`

```lua
local rotation_speed = 45.0  -- degrees per second

function on_update(dt)
    local rx, ry, rz = uz.scene.get_rotation(NODE_ID)
    ry = ry + rotation_speed * dt
    uz.scene.set_rotation(NODE_ID, rx, ry, rz)
end
```

Attach to any node via `.uzscene` property `script = assets/scripts/rotate_node.lua`.

---

### Orbit Camera

**File**: `assets/scripts/camera_orbit.lua`

```lua
local radius = 25
local height = 12
local speed  = 0.4  -- radians per second

function on_start()
    uz.log("camera_orbit: started")
end

function on_update(dt)
    local t = uz.total_time()
    local angle = t * speed

    local x = math.sin(angle) * radius
    local z = math.cos(angle) * radius
    local y = height

    uz.camera.set_position(x, y, z)
    uz.camera.look_at(0, 2, 0)
end

function on_stop()
    uz.log("camera_orbit: stopped")
end
```

This script circles the camera around the scene center at a fixed radius.

---

## Error Handling

### Script Load Errors

If a script has a syntax error during loading:
1. Error is logged to engine console
2. Node remains in scene but script does not execute
3. Engine continues normally

**Example:**
```lua
-- Bad syntax (missing end)
function on_update(dt)
    uz.log("test"
-- Missing 'end'
```

**Console output:**
```
[ERROR] assets/scripts/broken.lua:1: <eof> expected near <eof>
```

---

### Runtime Errors

If a script errors during `on_start()` or `on_update()`:
1. Error is logged with file:line info
2. `on_update()` **stops being called** for that script instance
3. Engine continues normally

**Example:**
```lua
function on_update(dt)
    local x = nil
    x.foo = 5  -- Error: attempt to index nil
end
```

**Console output:**
```
[ERROR] assets/scripts/example.lua:3: attempt to index a nil value
```

---

## Testing Scripts in VSCode

### See Script Output

Open **VSCode Output** → select **"UzEngine Server"** to see all Lua `uz.log()` messages.

### Hot-Reload

Scripts are currently **not** automatically reloaded on file save (Phase 2 feature). To test script changes:

1. Edit the `.lua` file
2. Reload the scene via **Play → Stop → Play** in VSCode extension
3. Script changes take effect

---

## Performance Considerations

### Per-Frame Allocations

Minimize table and string allocations in `on_update()`:

**Good:**
```lua
function on_update(dt)
    self.counter = self.counter + 1
end
```

**Bad:**
```lua
function on_update(dt)
    local data = {x = 1, y = 2}  -- Allocates every frame
end
```

---

### Node Lookups

Cache node references in `on_start()`:

**Good:**
```lua
function on_start()
    self.target = uz.scene.get_rotation  -- Cache function
end

function on_update(dt)
    self.target()  -- Call cached function
end
```

---

### Avoid Heavy Computation Per Frame

Schedule periodic updates manually:

```lua
function on_start()
    self.next_check = 0
end

function on_update(dt)
    self.next_check = self.next_check - dt
    if self.next_check <= 0 then
        self:expensive_operation()
        self.next_check = 1.0  -- Check again in 1 second
    end
end
```

---

## Integration with Engine

### Script Binding Registration

Scripts are registered via the `LuaVM` class:

```cpp
// C++ side (engine)
class LuaVM {
public:
    bool init();  // Initialize Lua state with bindings
    void set_scene(Scene* scene);
    void set_scene_params(SceneParams* params);
    void set_frame_time(float dt, float total);
private:
    void register_bindings();  // Binds uz.log, uz.scene, uz.camera, etc.
    void apply_sandbox();       // Restricts os, io, debug, etc.
};
```

### Script Execution Flow

Each `LuaScriptInstance` follows this flow:

```cpp
// Load phase
LuaScriptInstance script(lua_vm);
script.load("assets/scripts/example.lua", node_id);

// Start phase (once)
script.on_start();

// Update phase (every frame)
script.on_update(delta_time);

// Stop phase (cleanup)
script.on_stop();
```

---

## Related Documentation

- [Lua Scripting Guide](guides/lua-scripting-guide.md) — design patterns, best practices
- [Scene System Spec](specs/scene-system.md) — node types, structure
- [CHANGELOG.md](../CHANGELOG.md) — implementation details
- [Architecture](guides/architecture.md) — engine design

---

## Roadmap

**Phase 1 (done)**:
- ✅ LuaJIT 2.1 VM with sandboxing
- ✅ `on_start()`, `on_update(dt)`, `on_stop()` lifecycle
- ✅ Scene rotation API (`uz.scene.get_rotation`, `uz.scene.set_rotation`)
- ✅ Camera API (`uz.camera.set_position`, `uz.camera.look_at`)
- ✅ Timing API (`uz.delta_time()`, `uz.total_time()`)
- ✅ Demo scripts

**Phase 2+**:
- 🔲 Script hot-reload (auto-reload on file save)
- 🔲 Scene position/scale API (`uz.scene.get_position`, `uz.scene.set_position`)
- 🔲 Node queries (`uz.scene.find_node()`)
- 🔲 Input API (`uz.input.is_pressed()`)
- 🔲 Physics API (raycasts, queries)
- 🔲 Event bus (`uz.events:emit()`, `uz.events:subscribe()`)
- 🔲 Timer system (`uz.schedule_after()`)
- 🔲 Animation bindings
- 🔲 Particle system control
- 🔲 UI system bindings

---

**Last Updated**: 2026-03-07
**Status**: Phase 1 complete, scripting core working
