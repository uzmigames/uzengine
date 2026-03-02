# Gameplay Programming Guide

> **Target**: Game developers writing Lua scripts and gameplay systems
> **Prerequisites**: Read [Lua Scripting Guide](lua-scripting-guide.md) and [Physics Guide](physics-guide.md)
> **Related**: [Gameplay Systems Spec](../specs/gameplay-systems.md), [ECS Advanced Patterns](../engine/ecs-advanced.md)

---

## Introduction

This guide teaches you how to build gameplay in UzEngine using Lua scripts and the high-level gameplay APIs (Gameplay Ability System, FSM, Behavior Trees, Spatial Queries). You'll learn patterns for common gameplay scenarios: character controllers, AI enemies, ability activation, item pickups, and more.

**Key philosophy**: **Events over polling, queries over iteration, data over code.**

---

## Script Anatomy

Every gameplay script follows this structure:

```lua
-- res://scripts/player_controller.lua
-- Lifecycle: on_start → (on_update per frame) → on_stop

local function on_start()
    -- Initialize: cache node references, grant abilities, set up event listeners
    print("Player spawning at", self.position)

    ctx.gameplay:grant_ability(self.id, "move")
    ctx.gameplay:grant_ability(self.id, "jump")
    ctx.gameplay:grant_ability(self.id, "attack")

    -- Cache tag IDs for fast lookup
    self.tag_enemy = ctx.world:tag_id("is_enemy")
    self.tag_player = ctx.world:tag_id("is_player")

    -- Subscribe to events that affect this character
    self.on_hit = ctx.events:subscribe("PlayerHit", function(ev)
        if ev.target == self.id then
            on_hit(ev.damage)
        end
    end)
end

local function on_update(dt)
    -- Update: read input, move character, check win/lose conditions

    -- Read input (button pressed, mouse position, etc.)
    local input = ctx.input:get_frame_input()

    -- Move character based on input
    if input.key_w then
        self.velocity = self.velocity + Vec3(0, 0, 10) * dt
    end

    -- Apply physics
    self.position = self.position + self.velocity * dt

    -- Check win condition
    if health <= 0 then
        on_death()
    end
end

local function on_stop()
    -- Cleanup: disconnect events, save state
    ctx.events:unsubscribe(self.on_hit)
    ctx.saves:set_state("player_health", health)
end

return {
    on_start = on_start,
    on_update = on_update,
    on_stop = on_stop,
}
```

**Key points**:
- `self` is the node handle — the entity this script is attached to
- `on_start` runs once when script initializes
- `on_update` runs every frame; use `dt` for frame-rate-independent timing
- `on_stop` runs when script unloads (editor stop, scene change, etc.)
- Return a table with these 3 functions

---

## Sequencing with Coroutines

Complex gameplay often requires sequences: "play animation → wait → move → play sound → check result". Coroutines handle this elegantly.

### Basic Coroutine Pattern

```lua
local function dialogue_sequence()
    -- Start dialogue
    ctx.events:emit("DialogueStart", {npc = self.id})

    -- Play audio, wait for it to complete
    local audio_handle = ctx.audio:play("vo/npc_greeting", {volume = 0.8})
    coroutine.yield_until_audio_done(audio_handle)

    -- Move NPC to camera
    move_to_with_animation(self, camera.position, "walk")
    coroutine.yield()  -- Wait 1 frame

    -- Trigger cutscene camera
    ctx.events:emit("CutsceneStart", {duration = 3.0})
    coroutine.yield_for(3.0)  -- Wait 3 seconds

    -- Done
    ctx.events:emit("DialogueEnd", {npc = self.id})
end

-- In on_start:
self.dialogue_seq = ctx.coroutines:create(dialogue_sequence)
```

### Coroutine Helpers (ctx.coroutines API)

```lua
-- Create and start a coroutine
local coro = ctx.coroutines:create(function()
    -- Yield until next frame
    coroutine.yield()

    -- Yield for N seconds
    coroutine.yield_for(2.5)

    -- Yield until condition is true
    coroutine.yield_until(function()
        return self.health > 0
    end)

    -- Yield until audio finishes
    local handle = ctx.audio:play("vo/long_speech")
    coroutine.yield_until_audio_done(handle)

    -- Yield until animation finishes
    coroutine.yield_until_animation_done(self.id)

    print("Sequence complete!")
end)

-- Check if running
if coro:is_running() then
    print("Still going...")
end

-- Stop early
coro:stop()

-- Coroutines are garbage-collected when scope ends
```

### Cutscene Example

```lua
local function intro_cutscene()
    -- Move camera
    local cam = ctx.scene:find_by_name("CinematicCamera")

    move_camera(cam, Vec3(0, 5, -10), Quat:euler(0.3, 0, 0), 2.0)
    coroutine.yield_for(2.0)

    -- Show HUD message
    ctx.ui:show("hud_intro_message")
    ctx.ui:set_property("hud_intro_message", "text", "Welcome to the Arena")

    -- Fade out message
    ctx.ui:animate_property("hud_intro_message", "opacity", 0.0, 1.0)
    coroutine.yield_for(1.0)

    ctx.ui:hide("hud_intro_message")

    -- Play opening music
    ctx.audio:play("music/intro", {bus = "music", volume = 0.5})

    print("Intro complete!")
end

-- In on_start:
self.intro = ctx.coroutines:create(intro_cutscene)
```

---

## Finite State Machines (FSM)

FSMs are perfect for characters with discrete states: Idle, Walk, Run, Jump, Fall, Attack, Dead.

### Simple FSM

```lua
local function create_player_fsm()
    return ctx.gameplay:create_fsm({
        initial_state = "Idle",

        states = {
            Idle = {
                on_enter = function()
                    ctx.vfx:stop_all_by_tag("moving")
                    stop_animation()
                end,

                on_update = function(dt)
                    local input = ctx.input:get_frame_input()

                    -- Transition to Walk if moving
                    if input.key_w or input.key_a or input.key_s or input.key_d then
                        self.fsm:emit("StartMove")
                        return
                    end

                    -- Jump while idle
                    if input.key_space then
                        self.fsm:emit("Jump")
                        return
                    end

                    -- Apply light friction
                    self.velocity = self.velocity * 0.9
                end,

                on_exit = function()
                    print("Player stopped idling")
                end,
            },

            Walk = {
                on_enter = function()
                    play_animation("walk_forward")
                end,

                on_update = function(dt)
                    local input = ctx.input:get_frame_input()

                    -- Calculate movement direction
                    local move_dir = Vec3(0, 0, 0)
                    if input.key_w then move_dir = move_dir + Vec3(0, 0, 1) end
                    if input.key_s then move_dir = move_dir + Vec3(0, 0, -1) end
                    if input.key_a then move_dir = move_dir + Vec3(-1, 0, 0) end
                    if input.key_d then move_dir = move_dir + Vec3(1, 0, 0) end

                    move_dir = move_dir:normalized()

                    -- Accelerate in that direction
                    if move_dir:length() > 0.1 then
                        self.velocity = move_dir * 5.0
                    else
                        self.fsm:emit("StopMove")
                    end

                    -- Jump while walking
                    if input.key_space then
                        self.fsm:emit("Jump")
                    end

                    -- Run if shift held
                    if input.key_lshift then
                        self.fsm:emit("StartRun")
                    end
                end,
            },

            Run = {
                on_enter = function()
                    play_animation("run_forward")
                end,

                on_update = function(dt)
                    local input = ctx.input:get_frame_input()

                    -- Similar movement, but 2x faster
                    local move_dir = get_movement_input(input)
                    self.velocity = move_dir * 10.0

                    -- Stop running when shift released
                    if not input.key_lshift then
                        self.fsm:emit("StopRun")
                    end

                    -- Stop if no movement
                    if move_dir:length() < 0.1 then
                        self.fsm:emit("StopMove")
                    end
                end,
            },

            Jump = {
                on_enter = function()
                    self.velocity.y = 7.0  -- Jump impulse
                    play_animation("jump")
                end,

                on_update = function(dt)
                    -- Apply gravity
                    self.velocity.y = self.velocity.y - 9.8 * dt

                    -- Move character
                    self.position = self.position + self.velocity * dt

                    -- Check if landed
                    local hit = ctx.world:raycast(
                        self.position,
                        Vec3(0, -1, 0),
                        0.5
                    )

                    if #hit > 0 then
                        self.velocity.y = 0
                        self.fsm:emit("Landed")
                    end
                end,
            },

            Dead = {
                on_enter = function()
                    play_animation("die")
                    disable_collider()
                end,

                on_update = function(dt)
                    -- No update; player is frozen
                end,
            },
        },

        transitions = {
            { from = "Idle", event = "StartMove", to = "Walk" },
            { from = "Walk", event = "StopMove", to = "Idle" },
            { from = "Walk", event = "StartRun", to = "Run" },
            { from = "Run", event = "StopRun", to = "Walk" },
            { from = "Run", event = "StopMove", to = "Idle" },
            { from = "Idle", event = "Jump", to = "Jump" },
            { from = "Walk", event = "Jump", to = "Jump" },
            { from = "Run", event = "Jump", to = "Jump" },
            { from = "Jump", event = "Landed", to = "Idle" },
            { from = "*", event = "Death", to = "Dead" },  -- Death can happen from any state
        },
    })
end

-- In on_start:
self.fsm = create_player_fsm()

-- In on_update:
function on_update(dt)
    self.fsm:tick(dt)
end
```

**Key points**:
- Each state has `on_enter`, `on_update`, `on_exit`
- Emit events to trigger transitions
- `from = "*"` matches any state (useful for global transitions like death)
- Guard functions can prevent transitions: `{ from="Walk", event="Jump", to="Jump", guard=function() return is_grounded() end }`

---

## Abilities & Gameplay Ability System (GAS)

Grant abilities to characters and activate them with proper cooldown/cost handling.

### Character Setup

```lua
local function on_start()
    self.tag_player = ctx.world:tag_id("is_player")

    -- Grant starting abilities
    ctx.gameplay:grant_ability(self.id, "basic_attack")
    ctx.gameplay:grant_ability(self.id, "fireball")
    ctx.gameplay:grant_ability(self.id, "dash")

    -- Store current stats
    self.stats = {
        health = 100,
        max_health = 100,
        mana = 50,
        max_mana = 50,
    }

    -- Listen for ability events
    self.on_ability_activated = ctx.events:subscribe("AbilityActivated", function(ev)
        if ev.activator == self.id then
            on_ability_used(ev)
        end
    end)

    self.on_ability_failed = ctx.events:subscribe("AbilityActivationFailed", function(ev)
        if ev.activator == self.id then
            ctx.log_warn("Ability activation failed: " .. ev.reason)
        end
    end)
end
```

### Basic Attack

```lua
local function try_basic_attack()
    local target_pos = get_crosshair_position()

    local result = ctx.gameplay:try_activate({
        activator = self.id,
        ability_id = "basic_attack",
        position = target_pos,
    })

    if result == "Success" then
        play_attack_animation()
        play_attack_sound()
    elseif result == "OnCooldown" then
        local remaining = ctx.gameplay:get_cooldown_remaining(self.id, "basic_attack")
        ctx.log_debug(string.format("Attack on cooldown: %.1f sec", remaining))
    elseif result == "InsufficientCost" then
        ctx.log_warn("Not enough mana!")
    end
end
```

### Fireball Ability with AoE Damage

```lua
-- Subscribe to fireball activation
ctx.events:subscribe("AbilityActivated", function(ev)
    if ev.ability_id == "fireball" then
        cast_fireball(ev.position)
    end
end)

local function cast_fireball(target_pos)
    -- Emit particle effect
    ctx.vfx:create_effect("fireball_burst", {
        position = target_pos,
        scale = 1.5,
    })

    -- Play audio
    ctx.audio:play("sfx/fireball_cast", {position = target_pos})

    -- Query for enemies in AoE
    local aoe_radius = 8.0
    local enemies = ctx.world:query_sphere(target_pos, aoe_radius, {
        tags = { "is_enemy" },
    })

    -- Apply damage to each enemy
    for _, enemy in ipairs(enemies) do
        local damage = 25.0

        -- Apply falloff based on distance
        local dist = (enemy.position - target_pos):length()
        local falloff = 1.0 - (dist / aoe_radius)
        damage = damage * falloff

        -- Emit damage event
        ctx.events:emit("TakeDamage", {
            target = enemy.id,
            amount = damage,
            source = self.id,
            damage_type = "fire",
            hit_position = enemy.position,
        })
    end
end
```

### Dash Ability with Knockback

```lua
ctx.events:subscribe("AbilityActivated", function(ev)
    if ev.ability_id == "dash" then
        perform_dash()
    end
end)

local function perform_dash()
    -- Get forward direction
    local cam = ctx.scene:find_by_name("PlayerCamera")
    local forward = cam:get_forward()

    -- Apply impulse
    self.velocity = forward * 15.0

    -- Visual feedback
    play_animation("dash")
    ctx.vfx:create_effect("dash_trail", {position = self.position})
    ctx.audio:play("sfx/dash", {volume = 0.7})

    -- Invulnerability frames
    self.is_invulnerable = true
    ctx.coroutines:create(function()
        coroutine.yield_for(0.3)
        self.is_invulnerable = false
    end)
end
```

---

## Shared State & Communication (ctx.shared)

Multiple scripts need to communicate without tight coupling. Use `ctx.shared` as a shared data dictionary.

```lua
-- res://scripts/player_controller.lua
local function on_start()
    -- Store reference to self in shared state
    ctx.shared:set("player", self)

    -- Listen for damage events
    ctx.events:subscribe("TakeDamage", function(ev)
        if ev.target == self.id then
            take_damage(ev.amount)
        end
    end)
end

-- res://scripts/ui_hud.lua (separate script on UI canvas)
local function on_update(dt)
    local player = ctx.shared:get("player")
    if not player then return end

    -- Update health bar
    local health_pct = player.health / player.max_health
    ctx.ui:set_property("health_bar", "width", string.format("%.0f%%", health_pct * 100))
end

-- res://scripts/enemy_ai.lua
local function detect_player()
    local player = ctx.shared:get("player")
    if not player then return false end

    local dist = (self.position - player.position):length()
    return dist < 15.0  -- Detection range
end
```

---

## Spatial Queries

Find nearby entities efficiently using BVH spatial indexing.

### Finding Enemies in Range

```lua
local function find_nearby_enemies()
    return ctx.world:query_sphere(self.position, 10.0, {
        tags = { "is_enemy" },
        max_results = 50,
    })
end

local function attack_nearest_enemy()
    local enemies = find_nearby_enemies()
    if #enemies == 0 then return end

    -- Sort by distance (already sorted if sort_by_distance=true)
    local nearest = enemies[1]

    local dir = (nearest.position - self.position):normalized()
    launch_projectile(dir)
end
```

### Raycast for Line-of-Sight

```lua
local function has_line_of_sight(target_pos)
    local origin = self.position + Vec3(0, 1.0, 0)  -- Eye height
    local direction = (target_pos - origin):normalized()

    local hits = ctx.world:raycast(origin, direction, 100.0, {
        tags = { "geometry", "is_enemy" },
        max_results = 1,  -- Just need first hit
    })

    if #hits == 0 then return true end  -- No obstructions

    local hit = hits[1]
    -- Check if we hit the target or an obstruction
    return hit.id == target.id
end

-- AI spotting the player
local player = ctx.shared:get("player")
if has_line_of_sight(player.position) then
    self.fsm:emit("PlayerSpotted")
end
```

### AABB Query for Overlap

```lua
local function check_in_trigger_zone(zone_pos, zone_size)
    local bounds = AABB.from_center(zone_pos, zone_size)
    local hits = ctx.world:query_aabb(bounds, {
        tags = { "is_player" },
    })

    return #hits > 0
end

-- In trigger script
local function on_update(dt)
    if check_in_trigger_zone(self.position, Vec3(5, 5, 5)) then
        ctx.events:emit("PlayerEnteredZone", {zone = self.id})
    end
end
```

---

## Event Bus Patterns

Communication between systems via the Event Bus — publish/subscribe without coupling.

### Defining Custom Events

```lua
-- Event: Player takes damage
ctx.events:subscribe("TakeDamage", function(ev)
    -- ev = {target, amount, source, damage_type, hit_position}
    apply_damage(ev.target, ev.amount)
end)

-- Broadcast: Enemy defeated
ctx.events:emit("EnemyDefeated", {
    enemy_id = self.id,
    player_id = killer.id,
    position = self.position,
})

-- Subscribe in UI system
ctx.events:subscribe("EnemyDefeated", function(ev)
    ctx.log_info("Player defeated enemy at " .. ev.position)
    increment_score(100)
end)
```

### Combo System Example

```lua
local last_hit_time = 0
local hit_count = 0
local combo_timeout = 2.0

ctx.events:subscribe("PlayerAttackHit", function(ev)
    if ev.attacker == self.id then
        local now = ctx.world:get_time()

        if now - last_hit_time > combo_timeout then
            hit_count = 0  -- Combo reset
        end

        hit_count = hit_count + 1
        last_hit_time = now

        ctx.log_info("Hit #" .. hit_count)

        if hit_count == 3 then
            ctx.events:emit("ComboMilestone", {combo = 3})
        end
    end
end)
```

---

## Common Patterns

### Pattern 1: Character Health & Death

```lua
local function on_start()
    self.health = 100
    self.max_health = 100

    ctx.events:subscribe("TakeDamage", function(ev)
        if ev.target == self.id then
            self.health = self.health - ev.amount

            if self.health <= 0 then
                on_death()
            else
                on_damaged(ev.amount)
            end
        end
    end)
end

local function on_damaged(amount)
    -- Play hit reaction
    play_animation("hit_react")

    -- Knockback
    local knockback = (self.position - ctx.events:last_event().hit_position):normalized()
    self.velocity = knockback * 3.0

    -- Emit damage number above character
    ctx.vfx:create_effect("damage_number", {
        position = self.position + Vec3(0, 2, 0),
        text = string.format("%.0f", amount),
        color = {1, 0, 0, 1},
    })
end

local function on_death()
    self.is_dead = true
    play_animation("die")
    disable_collider()

    ctx.events:emit("CharacterDied", {
        id = self.id,
        position = self.position,
    })
end
```

### Pattern 2: Item Pickup

```lua
-- res://scripts/item.lua

local function on_start()
    ctx.world:tag_add(self.id, "item")

    -- Rotate and bob animation
    self.rotation_speed = 2.0
    self.bob_amount = 0.5
end

local function on_update(dt)
    -- Rotate
    self.rotation = self.rotation * Quat:axis_angle(Vec3(0, 1, 0), self.rotation_speed * dt)

    -- Bob up and down
    local time = ctx.world:get_time()
    self.position.y = self.position.y + math.sin(time * 2) * self.bob_amount * dt
end

-- res://scripts/item_trigger.lua (collision/trigger)

local function on_start()
    self.item_type = "health_potion"
    self.item_value = 25
end

local function on_update(dt)
    -- Check overlap with player
    local player = ctx.shared:get("player")
    if not player then return end

    local dist = (self.position - player.position):length()
    if dist < 1.5 then
        on_pickup(player)
    end
end

local function on_pickup(picker)
    -- Emit event
    ctx.events:emit("ItemPickedUp", {
        item_id = self.id,
        picker_id = picker.id,
        item_type = self.item_type,
        value = self.item_value,
    })

    -- Destroy item
    ctx.world:destroy_node(self.id)
end

-- In player script
ctx.events:subscribe("ItemPickedUp", function(ev)
    if ev.picker_id == self.id then
        if ev.item_type == "health_potion" then
            self.health = math.min(self.health + ev.value, self.max_health)
        end
    end
end)
```

### Pattern 3: Simple Enemy AI

```lua
-- res://scripts/enemy_ai.lua

local function on_start()
    self.health = 50
    self.max_health = 50
    self.detection_range = 15.0
    self.attack_range = 2.0
    self.patrol_points = {}
    self.current_patrol = 1

    self.fsm = ctx.gameplay:create_fsm({
        initial_state = "Patrol",
        states = {
            Patrol = {
                on_update = function(dt)
                    move_to_patrol_point(dt)
                    if is_player_visible() then
                        self.fsm:emit("PlayerSpotted")
                    end
                end,
            },
            Chase = {
                on_update = function(dt)
                    local player = ctx.shared:get("player")
                    if not player then
                        self.fsm:emit("PlayerLost")
                        return
                    end

                    local dist = (self.position - player.position):length()

                    if dist > self.detection_range then
                        self.fsm:emit("PlayerLost")
                    elseif dist < self.attack_range then
                        self.fsm:emit("InRange")
                    else
                        move_toward(player.position, 5.0, dt)
                    end
                end,
            },
            Attack = {
                on_enter = function()
                    play_animation("attack")
                end,
                on_update = function(dt)
                    local player = ctx.shared:get("player")
                    if not player or (self.position - player.position):length() > self.attack_range * 1.5 then
                        self.fsm:emit("PlayerLost")
                    end
                end,
            },
        },
        transitions = {
            { from = "Patrol", event = "PlayerSpotted", to = "Chase" },
            { from = "Chase", event = "PlayerLost", to = "Patrol" },
            { from = "Chase", event = "InRange", to = "Attack" },
            { from = "Attack", event = "PlayerLost", to = "Patrol" },
        },
    })

    -- Listen for damage
    ctx.events:subscribe("TakeDamage", function(ev)
        if ev.target == self.id then
            on_hit(ev.amount)
        end
    end)
end

local function on_update(dt)
    self.fsm:tick(dt)

    -- Apply physics
    self.position = self.position + self.velocity * dt
    self.velocity = self.velocity * 0.95  -- Friction
end

local function on_hit(amount)
    self.health = self.health - amount
    play_animation("hit_react")

    if self.health <= 0 then
        on_death()
    end
end

local function on_death()
    ctx.events:emit("EnemyDefeated", {enemy_id = self.id})
    ctx.world:destroy_node(self.id)
end

local function move_toward(target_pos, speed, dt)
    local dir = (target_pos - self.position):normalized()
    self.velocity = dir * speed

    -- Rotate to face target
    local angle = math.atan2(dir.x, dir.z)
    self.rotation = Quat:euler(0, angle, 0)
end

local function move_to_patrol_point(dt)
    if #self.patrol_points == 0 then return end

    local target = self.patrol_points[self.current_patrol]
    local dist = (self.position - target):length()

    if dist < 1.0 then
        self.current_patrol = self.current_patrol % #self.patrol_points + 1
    else
        move_toward(target, 3.0, dt)
    end
end

local function is_player_visible()
    local player = ctx.shared:get("player")
    if not player then return false end

    local dist = (self.position - player.position):length()
    return dist < self.detection_range
end

return {
    on_start = on_start,
    on_update = on_update,
}
```

---

## Best Practices

### DO

✅ Use events for decoupled communication
✅ Use queries (sphere, raycast) instead of iterating all entities
✅ Cache node references and tag IDs in on_start
✅ Use coroutines for sequences (animations, cutscenes)
✅ Use FSM for character states
✅ Use ctx.shared for inter-script communication
✅ Check distance before raycast (raycast is more expensive)
✅ Unsubscribe from events in on_stop
✅ Use frame-rate-independent timing with `dt`

### DON'T

❌ Poll every frame when you can use events
❌ Iterate all entities to find one (use spatial queries)
❌ Store node handles longer than needed (they may be deleted)
❌ Modify character velocity directly (use abilities/FSM)
❌ Forget to return the 3-function table from script
❌ Call slow functions in on_update (cache in on_start)
❌ Use raycasts for simple distance checks
❌ Create new Vec3/Quat every frame (cache or pool)
❌ Forget to handle missing references (ctx.shared:get returns nil)

---

## Performance Tips

### Queries

```lua
-- ❌ SLOW: Full spatial query every frame
local enemies = ctx.world:query_sphere(self.position, 10.0)
for _, e in ipairs(enemies) do check_threat(e) end

-- ✅ FAST: Cache enemies, only re-query when needed
if ctx.world:get_time() - self.last_scan_time > 0.5 then
    self.cached_threats = ctx.world:query_sphere(self.position, 10.0)
    self.last_scan_time = ctx.world:get_time()
end
for _, e in ipairs(self.cached_threats) do check_threat(e) end
```

### Allocations

```lua
-- ❌ SLOW: Creates table every frame
function on_update(dt)
    local pos = Vec3(self.position.x, self.position.y, self.position.z)
    ctx.vfx:emit_at(pos)
end

-- ✅ FAST: Reuse table
function on_start()
    self.temp_pos = Vec3(0, 0, 0)
end

function on_update(dt)
    self.temp_pos.x = self.position.x
    ctx.vfx:emit_at(self.temp_pos)
end
```

### Batch Operations

```lua
-- ❌ SLOW: 10 individual API calls
for i = 1, 10 do
    ctx.audio:play("sfx/spark")
end

-- ✅ FAST: 1 batch call
ctx.audio:play_batch({
    "sfx/spark", "sfx/spark", "sfx/spark", ... (10x)
}, {volume = 0.5})
```

---

## Testing Gameplay

```lua
-- test/gameplay_test.lua

describe("Player Controller", function()
    it("should move in direction of input", function()
        local player = create_test_player()
        player:emit_input({key_w = true})

        player:update(0.016)  -- 1 frame

        assert.is_true(player.velocity.z > 0)
    end)

    it("should be on cooldown after ability", function()
        local player = create_test_player()
        ctx.gameplay:grant_ability(player.id, "attack")

        local result1 = ctx.gameplay:try_activate({
            activator = player.id,
            ability_id = "attack",
            position = Vec3(0, 0, 0),
        })
        assert_equals(result1, "Success")

        local result2 = ctx.gameplay:try_activate({
            activator = player.id,
            ability_id = "attack",
            position = Vec3(0, 0, 0),
        })
        assert_equals(result2, "OnCooldown")
    end)

    it("should emit ItemPickedUp event", function()
        local player = create_test_player()
        local item = create_test_item({position = player.position + Vec3(1, 0, 0)})

        local event_fired = false
        ctx.events:subscribe("ItemPickedUp", function()
            event_fired = true
        end)

        item:update(0.1)

        assert.is_true(event_fired)
    end)
end)
```

---

## Related Docs

- [Gameplay Systems Spec](../specs/gameplay-systems.md) — Detailed API documentation
- [Lua Scripting Guide](lua-scripting-guide.md) — Lua fundamentals and ctx API
- [Physics Guide](physics-guide.md) — Collision and raycast physics
- [ECS Advanced Patterns](../engine/ecs-advanced.md) — Entity management internals
- [Event Bus Spec](../specs/event-bus.md) — Event system details
