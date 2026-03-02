# Lua Scripting Guide — UzEngine

Write fast, flexible game logic using Lua scripts. Attach scripts to nodes, react to events, and control game state without recompiling the engine.

> **Status**: Lua scripting comes in Phase 2-3. Scene system (Phase 1) is the prerequisite.

---

## Overview

### What Lua Is For

Lua scripts in UzEngine handle:
- **NPC behavior** — patrol paths, dialogue, reactions
- **Game triggers** — doors, pickups, level transitions
- **Input handling** — keyboard/gamepad → game logic
- **Events** — collisions, damage, health updates
- **Game flow** — menus, pause, respawn
- **Rapid iteration** — change logic, reload script, test instantly

### What Lua Is NOT For

Lua scripts **cannot** do:
- Rendering (use shaders for visuals)
- Memory management (engine handles that)
- GPU commands (DX12 is C++-only)
- Asset loading (use asset manager)
- Physics simulation (use Jolt; Lua just reads results)

Think of Lua as the "orchestrator" that reacts to the world, not the builder of the world.

---

## Script File Structure

### Module Pattern

Every Lua script follows a **module pattern** with lifecycle hooks:

```lua
-- res://game/player_controller.lua
local M = {}

-- Called when the node is spawned/loaded
function M:on_start(ctx)
    self.speed = 5.0
    self.velocity = vec3(0, 0, 0)
    ctx.log("Player controller started")
end

-- Called every frame (dt = delta time in seconds)
function M:on_update(ctx, dt)
    -- Read input
    local input = ctx.input.pressed("move_forward") and 1 or 0

    -- Update state
    self.velocity = vec3(input * self.speed, self.velocity.y, 0)

    -- Move the node
    ctx.scene:move_node(self.node_ref, self.velocity * dt)
end

-- Called when the node is destroyed
function M:on_stop(ctx)
    ctx.log("Player controller stopped")
end

-- Return the module
return M
```

### Self Table

The **self** table is persistent storage for the script instance:
- Created when the script starts
- Shared between on_start, on_update, on_stop
- One per script instance (e.g., each enemy has its own self)
- Automatically preserved across hot-reload (Phase 3)

```lua
function M:on_start(ctx)
    self.counter = 0           -- Persists across frames
    self.random_value = math.random()
end

function M:on_update(ctx, dt)
    self.counter = self.counter + 1
    ctx.log("Frame " .. self.counter)
end
```

### Return Statement

Always return the module at the end:

```lua
return M
```

This allows the engine to call `M:on_start()`, `M:on_update()`, etc.

---

## Engine Context API (ctx)

The `ctx` parameter passed to lifecycle functions provides access to the engine. It's your primary interface to the game world.

### Scene Management (ctx.scene)

**Find a node by name:**
```lua
local camera_node = ctx.scene:find_node("MainCamera")
if camera_node then
    ctx.log("Found camera!")
else
    ctx.log("Camera not found!")
end
```

**Add a new node to the scene:**
```lua
local new_node = ctx.scene:add_node({
    type = "Node3D",
    name = "DynamicObject",
    parent = self.node_ref,
    position = vec3(0, 5, 0)
})
```

**Remove a node:**
```lua
ctx.scene:remove_node(some_node_ref)
```

**Instantiate a prefab:**
```lua
local enemy = ctx.scene:instantiate_prefab("res://prefabs/goblin.uzscene", {
    parent = ctx.scene.root,
    position = vec3(10, 0, 0)
})
```

**Set node properties:**
```lua
ctx.scene:set_property(node_ref, "position", vec3(5, 0, 0))
ctx.scene:set_property(node_ref, "rotation", quat.from_euler(0, math.pi/4, 0))
ctx.scene:set_property(node_ref, "scale", vec3(2, 2, 2))
```

### Input System (ctx.input)

**Check if a button is pressed (this frame only):**
```lua
if ctx.input:pressed("jump") then
    self.velocity.y = 5.0
end
```

**Check if a button is held down:**
```lua
if ctx.input:held("move_forward") then
    self.position.z = self.position.z - self.speed * dt
end
```

**Get axis value (analog stick, mouse, WASD):**
```lua
local move_x = ctx.input:axis("move_horizontal")  -- -1 to 1
local move_y = ctx.input:axis("move_vertical")    -- -1 to 1
```

**Available actions** (configured in project.uzproj):
- Standard: `jump`, `attack`, `interact`, `pause`
- Axes: `move_horizontal`, `move_vertical`, `look_horizontal`, `look_vertical`

### Audio System (ctx.audio)

**Play a sound at a position (2D/3D):**
```lua
-- 2D sound (no spatial info)
ctx.audio:play_2d("res://assets/sounds/explosion.wav", {
    volume = 0.8,
    pitch = 1.0,
    loop = false
})

-- 3D sound (position, attenuation)
ctx.audio:play_at("res://assets/sounds/footstep.wav", {
    position = self.node_ref.position,
    volume = 0.5,
    radius = 20.0   -- falloff radius
})
```

**Stop all audio:**
```lua
ctx.audio:stop_all()
```

**Control bus volume (for mixing):**
```lua
ctx.audio:set_bus_volume("Music", 0.5)    -- Music half volume
ctx.audio:set_bus_volume("SFX", 0.8)      -- SFX at 80%
ctx.audio:set_bus_volume("Voice", 1.0)    -- Voice at full
```

### Event System (ctx.events)

**Emit an event (broadcast to listeners):**
```lua
ctx.events:emit("player_died", {
    position = self.node_ref.position,
    playtime = ctx.time_elapsed
})
```

**Listen for an event:**
```lua
function M:on_start(ctx)
    ctx.events:subscribe("enemy_spawned", function(data)
        ctx.log("Enemy spawned at " .. tostring(data.position))
    end)
end
```

**Built-in events:**
- `scene_loaded` — Scene finished loading
- `node_added` — Node added to scene
- `node_removed` — Node removed from scene
- `collision_enter` — Two colliders touched
- `collision_exit` — Two colliders separated
- `trigger_enter` — Entered a trigger volume
- `trigger_exit` — Left a trigger volume
- `animation_finished` — Skeletal animation completed

### Save/Load System (ctx.saves)

**Save the game to a slot:**
```lua
function M:on_update(ctx, dt)
    if ctx.input:pressed("save") then
        ctx.saves:save(0, {
            level = "level_2",
            health = self.player_health,
            inventory = self.inventory
        })
        ctx.log("Game saved!")
    end
end
```

**Load from a slot:**
```lua
local save_data = ctx.saves:load(0)
if save_data then
    self.player_health = save_data.health
    self.inventory = save_data.inventory
    ctx.log("Game loaded!")
else
    ctx.log("No save file!")
end
```

**Autosave (automatic on scene transitions):**
```lua
ctx.saves:autosave()  -- Engine auto-calls this; you rarely need to
```

### Localization System (ctx.locale)

**Translate a key (lookup in current locale):**
```lua
local greeting = ctx.locale:t("npc.greeting")
ctx.log(greeting)  -- Shows translated text
```

**Translate with formatting (replace {0}, {1}, etc.):**
```lua
local text = ctx.locale:tf("combat.damage_text", {
    attacker = "Player",
    damage = 25,
    target = "Goblin"
})
-- CSV: "combat.damage_text" = "{0} deals {1} damage to {2}!"
-- Result: "Player deals 25 damage to Goblin!"
```

**Change active locale (runtime language switch):**
```lua
ctx.locale:set("pt_br")  -- Switch to Portuguese
ctx.locale:set("ja")     -- Switch to Japanese
ctx.locale:set("en")     -- Switch back to English
```

**Fallback behavior:**
- Key lookup: current locale → English → key itself
- Never crashes; always returns something displayable

### Console System (ctx.console)

**Register a console command:**
```lua
function M:on_start(ctx)
    ctx.console:register("test_damage", function(args)
        self:take_damage(10)
        return "Took 10 damage, health now: " .. self.health
    end)
end

-- Player types in console: "test_damage"
-- Output: "Took 10 damage, health now: 90"
```

### VFX System (ctx.vfx)

**Spawn a visual effect (particle, decal, etc.):**
```lua
ctx.vfx:spawn("res://vfx/explosion.uzfx", {
    position = self.node_ref.position,
    rotation = self.node_ref.rotation,
    scale = 2.0
})
```

**Stop a running effect (by handle):**
```lua
local handle = ctx.vfx:spawn("res://vfx/flame.uzfx", {...})
-- Later...
ctx.vfx:stop(handle)
```

### Navigation System (ctx.nav)

**Move to a destination (pathfinding):**
```lua
function M:on_update(ctx, dt)
    ctx.nav:set_destination(self.node_ref, vec3(10, 0, 5))
    -- Engine pathfinds and moves the node toward destination
end

-- Stop moving
ctx.nav:stop(self.node_ref)
```

### UI System (ctx.ui)

**Load a UI panel (Lua or XAML):**
```lua
local panel = ctx.ui:load("res://ui/inventory.uzui")
panel:show()
```

**Hide/unload a panel:**
```lua
panel:hide()
panel:unload()
```

**Trigger events on UI elements:**
```lua
-- Handle button click
ctx.ui:on_event("button_play", function(data)
    ctx.log("Play button clicked!")
end)
```

**Set UI properties:**
```lua
panel:set_property("title", "Game Over")
panel:set_property("health_bar_value", 0.75)
```

### Logging (ctx.log)

**Print to engine log:**
```lua
ctx.log("Debug message")
ctx.log("Player position: " .. tostring(self.position))
```

Output appears in VSCode "UzEngine Server" output channel.

### Engine Metrics (ctx.time_*)

**Current simulation time:**
```lua
local elapsed = ctx.time_elapsed  -- Seconds since scene started
local frame = ctx.frame_index     -- Frame number (0, 1, 2, ...)
local dt = ctx.delta_time         -- Delta time (passed to on_update anyway)
```

### Node References

**Get a node's properties:**
```lua
local pos = node_ref.position  -- vec3
local rot = node_ref.rotation  -- quat
local scale = node_ref.scale   -- vec3
local name = node_ref.name     -- string
```

**Get child nodes:**
```lua
local children = node_ref:children()
for i, child in ipairs(children) do
    ctx.log("Child " .. i .. ": " .. child.name)
end
```

**Find a child by name:**
```lua
local arm = self.node_ref:find_child("RightArm")
if arm then
    arm.position = vec3(0, 0, 0)
end
```

---

## Timer System (Scheduling)

### Schedule a function to run later

**Run once after a delay:**
```lua
ctx.schedule_after(2.0, function()
    ctx.log("2 seconds have passed!")
end)
```

**Run repeatedly every N seconds:**
```lua
local handle = ctx.schedule_every(0.5, function()
    self.damage_tick_count = self.damage_tick_count + 1
    ctx.log("Damage tick " .. self.damage_tick_count)
end)

-- Cancel the timer
ctx.cancel_timer(handle)
```

**Timers persist across frames** — great for delayed actions, periodic checks, cooldowns.

---

## Event Bus (Publish-Subscribe)

### Emit Events

```lua
-- Simple event
ctx.events:emit("player_level_up")

-- Event with data
ctx.events:emit("health_changed", {
    old_value = 100,
    new_value = 80,
    source = "spike_trap"
})
```

### Subscribe to Events

```lua
function M:on_start(ctx)
    -- Single listener (runs once per emit)
    ctx.events:subscribe("health_changed", function(data)
        ctx.log("Health: " .. data.old_value .. " -> " .. data.new_value)
    end)

    -- Unsubscribe (if you save the handle)
    local handle = ctx.events:subscribe("some_event", callback)
    ctx.events:unsubscribe(handle)
end
```

### Built-in Events

| Event | Data | Fired When |
|-------|------|-----------|
| `scene_loaded` | `{scene_name}` | Scene finishes loading |
| `node_added` | `{node}` | Node added to scene |
| `node_removed` | `{node}` | Node destroyed |
| `collision_enter` | `{a, b, contact}` | Two colliders touch (Jolt) |
| `collision_exit` | `{a, b}` | Colliders separate |
| `trigger_enter` | `{trigger, other}` | Object enters trigger |
| `trigger_exit` | `{trigger, other}` | Object leaves trigger |
| `animation_finished` | `{node, animation}` | Skeletal anim completed |
| `input_action` | `{action, pressed}` | Input action fired |
| `scene_transition` | `{from, to}` | Changing levels |

---

## Math Types

### Vec3 (3D Vector)

```lua
local v = vec3(1, 2, 3)
local v2 = vec3.zero()           -- (0, 0, 0)
local v3 = vec3.one()            -- (1, 1, 1)

-- Properties
local x, y, z = v.x, v.y, v.z

-- Operations
local sum = v + v2
local diff = v - v2
local scaled = v * 2.0
local normalized = v:normalize()
local magnitude = v:length()
local dot = v:dot(v2)
local cross = v:cross(v2)
```

### Quat (Quaternion, Rotation)

```lua
-- Create rotations
local q = quat.identity()
local q2 = quat.from_euler(0, math.pi / 4, 0)  -- Euler angles (rad)
local q3 = quat.from_axis_angle(vec3(0, 1, 0), math.pi / 4)

-- Operations
local combined = q * q2  -- Compose rotations
local inverted = q:inverse()
local euler = q:to_euler()  -- Back to Euler angles
```

### Vec2 (2D Vector)

```lua
local v = vec2(10, 20)
local magnitude = v:length()
local normalized = v:normalize()
```

---

## Error Handling

### What Happens on Lua Error?

If a script has a runtime error:
1. **Engine logs the error** with file:line info
2. **Node is disabled** (on_update stops running)
3. **Engine continues** (doesn't crash)
4. **Error visible in VSCode** → lua.get_errors IPC command

**Example error:**
```lua
function M:on_update(ctx, dt)
    local x = nil
    x.foo = 5  -- ERROR: attempt to call nil value
end

-- Output:
-- [ERROR] game/npc.lua:3: attempt to index a nil value
-- [disabled] Node "Goblin_1" script crashed; see errors
```

### Using pcall (Protected Call)

Wrap risky operations in `pcall` to catch errors:

```lua
function M:on_start(ctx)
    local ok, result = pcall(function()
        return ctx.scene:find_node("DoesNotExist")
    end)

    if ok then
        ctx.log("Found node: " .. result.name)
    else
        ctx.log("Error: " .. result)  -- Error message
    end
end
```

---

## Hot-Reload

### How Script Reloading Works

1. **File saved** → engine detects change via FileWatcher
2. **Script reloaded** → new code loaded into VM
3. **on_stop() called** (old code)
4. **on_start() called** (new code)
5. **on_update() continues** (new code from now on)

> **Important**: The **self** table is NOT preserved across reload. Any state you need must be serialized/deserialized manually or stored in ctx.saves.

**Example: Saving state before reload:**
```lua
function M:on_stop(ctx)
    -- Save state to a temporary slot
    ctx.saves:save_script_state("my_npc", {
        health = self.health,
        position = self.position
    })
end

function M:on_start(ctx)
    -- Restore state
    local saved = ctx.saves:load_script_state("my_npc")
    if saved then
        self.health = saved.health
        self.position = saved.position
        ctx.log("State restored after reload!")
    else
        -- First run
        self.health = 100
        self.position = vec3(0, 0, 0)
    end
end
```

---

## Common Patterns

### Enemy Patrol

```lua
local M = {}

function M:on_start(ctx)
    self.waypoints = {
        vec3(0, 0, 0),
        vec3(10, 0, 0),
        vec3(10, 0, 10),
        vec3(0, 0, 10)
    }
    self.current_waypoint = 1
    self.speed = 3.0
end

function M:on_update(ctx, dt)
    local target = self.waypoints[self.current_waypoint]
    local current_pos = self.node_ref.position
    local direction = (target - current_pos):normalize()

    -- Move toward waypoint
    self.node_ref.position = current_pos + direction * self.speed * dt

    -- Check if reached
    if (current_pos - target):length() < 0.5 then
        self.current_waypoint = self.current_waypoint + 1
        if self.current_waypoint > #self.waypoints then
            self.current_waypoint = 1  -- Loop
        end
    end
end

return M
```

### Door Trigger

```lua
local M = {}

function M:on_start(ctx)
    self.is_open = false
    self.door_node = ctx.scene:find_node("Door")

    -- Listen for trigger
    ctx.events:subscribe("trigger_enter", function(data)
        if data.trigger == self.node_ref and not self.is_open then
            self:open_door(ctx)
        end
    end)
end

function M:open_door(ctx)
    self.is_open = true
    ctx.log("Door opening...")

    -- Animate door (linear for now)
    ctx.schedule_after(1.0, function()
        self.door_node.position = self.door_node.position + vec3(0, 0, 3)
    end)
end

return M
```

### Health System

```lua
local M = {}

function M:on_start(ctx)
    self.max_health = 100
    self.health = self.max_health
end

function M:take_damage(ctx, amount)
    self.health = math.max(0, self.health - amount)
    ctx.events:emit("health_changed", {
        value = self.health,
        max = self.max_health
    })

    if self.health == 0 then
        self:die(ctx)
    end
end

function M:die(ctx)
    ctx.log(self.node_ref.name .. " died!")
    ctx.events:emit("enemy_died", {node = self.node_ref})
    ctx.scene:remove_node(self.node_ref)
end

return M
```

### Pickup Item

```lua
local M = {}

function M:on_start(ctx)
    self.item_type = "health_potion"
    self.value = 25

    ctx.events:subscribe("trigger_enter", function(data)
        if data.trigger == self.node_ref then
            self:pickup(ctx, data.other)
        end
    end)
end

function M:pickup(ctx, player_node)
    ctx.log("Picked up " .. self.item_type)
    ctx.events:emit("item_collected", {
        type = self.item_type,
        value = self.value
    })
    ctx.scene:remove_node(self.node_ref)
end

return M
```

---

## Debugging

### Logging

```lua
ctx.log("Debug: " .. self.state)
ctx.log("Position: " .. tostring(self.node_ref.position))
```

View output in VSCode → "UzEngine Server" output channel.

### Inspecting Lua State

Use the IPC command to inspect globals:

```
uz_call("lua.get_globals")
→ {globals with all your script variables}
```

### Checking for Errors

```
uz_call("lua.get_errors")
→ {list of all Lua runtime errors with file:line}
```

---

## Performance Tips

### Avoid Allocations in on_update

**Bad:**
```lua
function M:on_update(ctx, dt)
    local t = {}  -- Table allocation every frame (slow)
    t.x = 1
end
```

**Good:**
```lua
function M:on_start(ctx)
    self.temp = {}  -- Allocate once
end

function M:on_update(ctx, dt)
    self.temp.x = 1  -- Reuse
end
```

### Cache Node References

**Bad:**
```lua
function M:on_update(ctx, dt)
    local camera = ctx.scene:find_node("MainCamera")  -- Lookup every frame
    camera.position = calculate_pos(dt)
end
```

**Good:**
```lua
function M:on_start(ctx)
    self.camera = ctx.scene:find_node("MainCamera")  -- Once
end

function M:on_update(ctx, dt)
    self.camera.position = calculate_pos(dt)
end
```

### Avoid Heavy Computations Per Frame

Use timers for periodic checks:

```lua
function M:on_start(ctx)
    -- Check pathfinding only every 0.5s, not every frame
    ctx.schedule_every(0.5, function()
        self:recalculate_path(ctx)
    end)
end
```

---

## Related Documentation

- [IPC and MCP Guide](ipc-mcp.md) — Control Lua from Claude Code
- [Engine Vision](../specs/engine-vision.md) — Architecture, phases
- [Scene System Spec](../specs/scene-system.md) (Phase 1)
- [Physics Guide](physics-guide.md) (Phase 3)
- [Asset Pipeline Guide](asset-pipeline-guide.md) (Phase 2)

---

## FAQ

**Q: Can I reload game.dll while editor is running?**
A: Yes! In Phase 2+, rebuild game.dll and the engine auto-detects change, reloads game.dll, and restarts Lua VM.

**Q: How do I preserve state across reloads?**
A: Use ctx.saves to serialize important data in on_stop(), then restore in on_start().

**Q: Can Lua call C++ functions directly?**
A: No, Lua only calls engine-exposed functions (ctx.* bindings). Custom C++ functions require engine modification.

**Q: Why does my script error say "attempt to index nil"?**
A: You're trying to access a property on something that's nil. Check if find_node() returned nil before using the result.

**Q: Can I create Lua classes?**
A: Yes! Use metatables: `setmetatable(M, {__index = BaseClass})` for inheritance.
