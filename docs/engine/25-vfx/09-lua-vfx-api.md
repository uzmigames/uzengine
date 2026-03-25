# Lua VFX API Reference

> **Status**: Planned (Phase 2.5)
> **API**: `ctx.vfx.*`
> **References**: [VFX System Overview](01-overview.md), [.uzvfx Format](07-uzvfx-format.md), [Lua Scripting](../../28-lua/01-scripting-overview.md)

## Purpose

The Lua VFX API provides runtime control over VFX effects spawned in the scene. Scripts can spawn effects, adjust parameters, trigger events, and manage effect lifecycle (start, stop, pause).

All VFX functions are accessed through the `ctx.vfx` namespace (provided by the Lua sandboxing layer).

## API Functions

### ctx.vfx:spawn(graph_path, position) → entity

Spawns a VFX effect at a world-space position.

**Signature:**
```lua
entity = ctx.vfx:spawn(graph_path, position)
```

**Parameters:**
- `graph_path` (string): Path to a `.uzvfx` or `.uzvfx_composite` file. Relative to asset root. Examples: `"vfx/fire.uzvfx"`, `"vfx/explosion_large.uzvfx_composite"`.
- `position` (table or userdata): World-space position where effect spawns. Can be:
  - Table: `{x, y, z}` or `{x=1, y=2, z=3}`
  - Userdata: `Vec3` object from engine

**Returns:**
- `entity` (userdata): Handle to the spawned effect. Used for subsequent parameter/event calls. Invalid if file not found or parsing fails.

**Errors:**
- Logs error if `graph_path` file not found or invalid.
- Returns `nil` on error (in strict error modes, may throw).

**Example:**
```lua
-- Spawn fire effect at world position (10, 5, 20)
fire = ctx.vfx:spawn("vfx/fire_campfire.uzvfx", {10, 5, 20})

-- Spawn explosion composite
explosion = ctx.vfx:spawn("vfx/explosion_large.uzvfx_composite", {0, 0, 0})
```

---

### ctx.vfx:set_parameter(entity, name, value) → void

Sets a floating-point or vector parameter on a spawned effect.

**Signature:**
```lua
ctx.vfx:set_parameter(entity, name, value)
```

**Parameters:**
- `entity` (userdata): Effect handle returned from `spawn()`.
- `name` (string): Parameter name as defined in the `.uzvfx` file (e.g., `"intensity"`, `"wind_dir"`, `"color_tint"`).
- `value` (number or table): Parameter value:
  - For float parameters: a number (e.g., `2.5`, `0.0`). Clamped to min/max if defined.
  - For vec3 parameters: a table `{x, y, z}` or `{x=1, y=2, z=3}`.
  - For vec4 parameters: a table `{x, y, z, w}` or `{r=1, g=0, b=0, a=1}`.

**Side Effects:**
- Updates the parameter on all active particles (or next spawn batch, depending on implementation).
- For composite effects, parameter is forwarded to all child emitters that define the parameter.

**Error Handling:**
- Logs warning if parameter name not found in effect definition.
- Logs error if value type does not match parameter type (e.g., passing number to vec3 parameter).

**Example:**
```lua
-- Set float parameter
ctx.vfx:set_parameter(fire, "intensity", 1.5)

-- Set vector parameter (wind direction)
ctx.vfx:set_parameter(fire, "wind_dir", {1.0, 0.0, 0.5})

-- Set color parameter
ctx.vfx:set_parameter(fire, "color_tint", {1.0, 0.5, 0.0, 1.0})

-- Clamp to [0, 10] range if parameter has min/max
ctx.vfx:set_parameter(fire, "intensity", 100.0)  -- Clamped to 10.0
```

---

### ctx.vfx:emit_event(entity, name) → void

Triggers a named event on a spawned effect, typically spawning additional particles.

**Signature:**
```lua
ctx.vfx:emit_event(entity, name)
```

**Parameters:**
- `entity` (userdata): Effect handle returned from `spawn()`.
- `name` (string): Event name as defined in the `.uzvfx` file (e.g., `"burst"`, `"explode"`).

**Side Effects:**
- Spawns `emission_count` particles with velocity multiplied by `velocity_scale` (as defined in the event).
- For composite effects, event is triggered on all child emitters that define the event.

**Error Handling:**
- Logs warning if event name not found.

**Example:**
```lua
-- Trigger "burst" event
ctx.vfx:emit_event(fire, "burst")

-- Trigger in response to player action
if input.IsPressed("Space") then
  ctx.vfx:emit_event(explosion, "explode")
end
```

---

### ctx.vfx:start(entity) → void

Starts or resumes particle emission on an effect.

**Signature:**
```lua
ctx.vfx:start(entity)
```

**Parameters:**
- `entity` (userdata): Effect handle.

**Side Effects:**
- Enables spawn module to emit particles normally.
- If effect was paused, resumes from paused state.
- If effect was already running, call is a no-op.

**Example:**
```lua
vfx = ctx.vfx:spawn("vfx/fire.uzvfx", position)
ctx.vfx:pause(vfx, true)   -- Pause emission
-- ... later ...
ctx.vfx:start(vfx)          -- Resume
```

---

### ctx.vfx:stop(entity) → void

Stops particle emission and marks effect for cleanup.

**Signature:**
```lua
ctx.vfx:stop(entity)
```

**Parameters:**
- `entity` (userdata): Effect handle.

**Side Effects:**
- Disables spawn module (no new particles).
- Existing particles continue to age and die naturally.
- Effect entity is destroyed once all particles have expired.

**Example:**
```lua
vfx = ctx.vfx:spawn("vfx/fire.uzvfx", position)
-- ... effect runs for 5 seconds ...
ctx.vfx:stop(vfx)
-- Particles fade out over their remaining lifespan
```

---

### ctx.vfx:pause(entity, paused) → void

Pauses or resumes particle simulation on an effect.

**Signature:**
```lua
ctx.vfx:pause(entity, paused)
```

**Parameters:**
- `entity` (userdata): Effect handle.
- `paused` (boolean): `true` to pause, `false` to resume.

**Side Effects:**
- When paused:
  - Spawn module stops emitting.
  - Update module freezes particle physics (no movement, aging, or forces).
  - Particles remain in place visually.
- When resumed:
  - Spawn and update resume normally.

**Example:**
```lua
vfx = ctx.vfx:spawn("vfx/fire.uzvfx", position)
-- Pause on menu open
ctx.vfx:pause(vfx, true)
-- Resume on menu close
ctx.vfx:pause(vfx, false)
```

---

### ctx.vfx:get_alive_count(entity) → integer

Returns the number of currently alive particles in an effect.

**Signature:**
```lua
count = ctx.vfx:get_alive_count(entity)
```

**Parameters:**
- `entity` (userdata): Effect handle.

**Returns:**
- `count` (integer): Number of particles currently simulating (0 if effect is stopped or all particles dead).

**Use Cases:**
- Check if effect is still running: `if ctx.vfx:get_alive_count(vfx) > 0 then ...`
- Spawn additional effects when particle count drops below threshold.
- Debug particle count for performance monitoring.

**Example:**
```lua
vfx = ctx.vfx:spawn("vfx/fire.uzvfx", position)
if ctx.vfx:get_alive_count(vfx) > 100 then
  print("Effect is still running with particles")
end
```

## Practical Examples

### Example 1: Simple Fire Effect

```lua
-- On scene load: spawn a campfire at a fixed location
function OnSceneLoad()
  local campfire_pos = {10.0, 2.0, 20.0}
  scene.fire_effect = ctx.vfx:spawn("vfx/fire_campfire.uzvfx", campfire_pos)
end

-- On scene unload: stop the effect
function OnSceneUnload()
  if scene.fire_effect then
    ctx.vfx:stop(scene.fire_effect)
  end
end
```

### Example 2: Explosion with Dynamic Parameters

```lua
-- Explosion triggered by damage event
function OnExplosionDamage(position, intensity)
  local explosion = ctx.vfx:spawn("vfx/explosion_large.uzvfx_composite", position)

  -- Scale intensity based on damage
  ctx.vfx:set_parameter(explosion, "intensity", math.min(intensity / 100.0, 3.0))

  -- Optional: burst event for extra impact
  ctx.vfx:emit_event(explosion, "burst")
end
```

### Example 3: Parameter Oscillation (Pulsing Glow)

```lua
-- Create a pulsing glow effect
local glow_effect = ctx.vfx:spawn("vfx/glow.uzvfx", {0, 0, 0})
local elapsed = 0.0

function OnUpdate(dt)
  elapsed = elapsed + dt
  local intensity = 1.0 + math.sin(elapsed * 2.0 * math.pi) * 0.5  -- Oscillate 0.5 to 1.5
  ctx.vfx:set_parameter(glow_effect, "intensity", intensity)
end
```

### Example 4: Dynamic Wind Effect

```lua
-- Apply wind direction based on time of day
local wind_effect = ctx.vfx:spawn("vfx/wind_particles.uzvfx", player_position)

function OnUpdate(dt)
  local wind_angle = (game.GetTimeOfDay() / 86400) * 2 * math.pi  -- Full rotation per day
  local wind_dir = {
    math.cos(wind_angle),
    0.0,
    math.sin(wind_angle)
  }
  ctx.vfx:set_parameter(wind_effect, "wind_direction", wind_dir)
end
```

### Example 5: Event-Driven Bursts

```lua
-- Weapon fire with burst effect
function OnWeaponFire(weapon_position)
  local muzzle_flash = ctx.vfx:spawn("vfx/muzzle_flash.uzvfx", weapon_position)

  -- Emit burst event immediately for impact flash
  ctx.vfx:emit_event(muzzle_flash, "burst")

  -- Stop effect after 1 second (weapon fire is brief)
  timer.After(1.0, function()
    ctx.vfx:stop(muzzle_flash)
  end)
end
```

### Example 6: Magic Spell with Color Tinting

```lua
-- Cast a spell with color based on spell type
function CastSpell(spell_type, position)
  local spell_effect = ctx.vfx:spawn("vfx/magic_spell.uzvfx_composite", position)

  -- Set color tint based on spell type
  if spell_type == "fire" then
    ctx.vfx:set_parameter(spell_effect, "color_tint", {1.0, 0.5, 0.0, 1.0})  -- Orange
  elseif spell_type == "ice" then
    ctx.vfx:set_parameter(spell_effect, "color_tint", {0.5, 0.8, 1.0, 1.0})  -- Light blue
  elseif spell_type == "lightning" then
    ctx.vfx:set_parameter(spell_effect, "color_tint", {0.8, 0.8, 1.0, 1.0})  -- White-blue
  end

  -- Trigger impact event
  ctx.vfx:emit_event(spell_effect, "impact")

  return spell_effect
end
```

### Example 7: Particle Count Monitoring

```lua
-- Health bar with particle effect feedback
local damage_effect = nil

function TakeDamage(amount)
  if not damage_effect or ctx.vfx:get_alive_count(damage_effect) == 0 then
    damage_effect = ctx.vfx:spawn("vfx/damage_spark.uzvfx", character_position)
  else
    -- Effect already running, trigger another burst
    ctx.vfx:emit_event(damage_effect, "burst")
  end
end
```

## Error Handling Patterns

### Safe Effect Spawning

```lua
function SafeSpawnEffect(path, position)
  if not path or not position then
    print("Error: Invalid parameters to SafeSpawnEffect")
    return nil
  end

  local effect = ctx.vfx:spawn(path, position)
  if not effect then
    print("Error: Failed to spawn effect at " .. path)
    return nil
  end

  return effect
end
```

### Null-Safe Effect Control

```lua
function SafeStop(effect)
  if effect and ctx.vfx:get_alive_count(effect) > 0 then
    ctx.vfx:stop(effect)
  end
end
```

## Performance Considerations

1. **Parameter updates are expensive**: Avoid calling `set_parameter()` every frame if possible. Use oscillation patterns (sine/cosine) sparingly.
2. **Event emission is free**: Calling `emit_event()` is cheap; use it liberally for visual feedback.
3. **Particle count scales**: Effects with `max_particles > 10000` can impact GPU performance. Profile with many effects on-screen.
4. **Composite overhead**: Each emitter in a composite consumes independent GPU resources. Limit to 3–5 emitters per composite.

## Threading & Lua Sandbox

All VFX API calls are **main-thread only**. Calls from background Lua workers will fail with an error. Use message passing to communicate between workers and main thread.

**Example (worker → main):**
```lua
-- In worker thread
worker:SendMessage("MainThread", "spawn_effect", {path="vfx/fire.uzvfx", position={0,0,0}})

-- In main thread message handler
function OnMessage(from, msg_type, data)
  if msg_type == "spawn_effect" then
    ctx.vfx:spawn(data.path, data.position)
  end
end
```

## See Also

- [.uzvfx Format](07-uzvfx-format.md) — Asset file format
- [.uzvfx_composite Format](08-uzvfx-composite-format.md) — Multi-emitter format
- [Lua Scripting Overview](../../28-lua/01-scripting-overview.md) — Lua API sandboxing
- [VFX System Overview](01-overview.md) — Architecture and systems
