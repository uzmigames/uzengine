# Streaming & Async Loading Guide

## Contents

1. [Concepts](#concepts)
2. [Quick Start](#quick-start)
3. [Level Streaming](#level-streaming)
4. [Asset Budgets](#asset-budgets)
5. [Preloading Strategy](#preloading-strategy)
6. [Debug & Profiling](#debug--profiling)
7. [Best Practices](#best-practices)
8. [Common Patterns](#common-patterns)
9. [Troubleshooting](#troubleshooting)

---

## Concepts

### Why Streaming?

**Without streaming** (synchronous loading):
- Load entire 500MB level at once
- 5-10 second loading screen freeze
- Memory budget exceeded on console

**With streaming** (asynchronous loading):
- Load chunks as player moves (20MB at a time)
- No frame stutters, background loading
- Graceful memory management with eviction

### Key Terms

**AssetHandle<T>**
A handle (reference) to an asset that may still be loading. Has a `state()` (Pending → Loading → Ready/Error).

**Priority**
Assets closer to the camera or on-screen get higher priority loads ("high", "normal", "low").

**Placeholder**
Temporary asset shown while real asset loads (e.g., gray box for mesh, magenta checker for texture).

**Memory Budget**
Hard limit on VRAM/RAM. When exceeded, engine evicts unused assets (LRU).

**Coroutine**
Lua function that can pause and resume. Used to wait for assets without blocking.

---

## Quick Start

### Load an Asset Asynchronously (10 seconds)

In any Lua script:

```lua
function on_start()
    -- Request asset load (non-blocking)
    local mesh_handle = ctx.assets:load_async("res://meshes/sword.gltf", "high")

    -- Option 1: Check without waiting
    if mesh_handle:is_ready() then
        local mesh = mesh_handle:get()
        self.node.mesh = mesh
    end

    -- Option 2: Wait with timeout
    if mesh_handle:wait(5000) then  -- 5 seconds
        local mesh = mesh_handle:get()
        self.node.mesh = mesh
    else
        ctx.log:warning("Sword mesh load timeout")
    end

    -- Option 3: Use coroutine (recommended for Lua)
    ctx.coroutine.launch(load_and_equip_sword)
end

function load_and_equip_sword()
    ctx.log:info("Loading sword...")
    local handle = ctx.assets:load_async("res://meshes/sword.gltf", "high")

    -- Coroutine pauses here until handle is ready
    ctx.coroutine.wait_until(handle)

    if handle:is_ready() then
        local mesh = handle:get()
        self.node.mesh = mesh
        ctx.log:info("Sword equipped!")
    else
        ctx.log:error("Failed to load sword: " .. handle:error())
    end
end
```

### What Happens Behind the Scenes

```
1. Script calls ctx.assets:load_async(...)
   ├─ Validate path exists
   ├─ Create AssetHandle in Pending state
   └─ Return immediately (non-blocking)

2. Background thread pool picks up request
   ├─ Priority = "high" → jumps queue
   ├─ Read file from disk (or .uzpak cache)
   ├─ Decode format (glTF → mesh data)
   ├─ GPU upload (if texture or mesh)
   └─ Mark handle as Ready

3. Lua coroutine waiting on handle
   ├─ wait_until() pauses execution
   ├─ Main thread checks handle every frame
   ├─ When handle → Ready, resume coroutine
   └─ Script continues
```

---

## Level Streaming

### Partition Your Scene

For large worlds, split into spatial chunks.

**Project Structure:**
```
res://scenes/
├── level_01.uzscene (master scene, ~100KB)
│   ├── Player start area (always loaded)
│   ├── Streaming volume at x:100-200 → loads chunk_a.uzscene
│   ├── Streaming volume at x:200-300 → loads chunk_b.uzscene
│   ├── Streaming volume at x:300-400 → loads chunk_c.uzscene
│   └── Streaming volume at x:400+ → loads chunk_d.uzscene
│
├── chunk_a.uzscene (~50MB, forest)
├── chunk_b.uzscene (~50MB, village)
├── chunk_c.uzscene (~50MB, dungeon entrance)
└── chunk_d.uzscene (~50MB, dungeon interior)
```

### Create Streaming Volumes in Editor

1. In `.uzscene` INI, add `[node: streaming_volume_a]` node
2. Set `node_type = "trigger_volume"` or `node_type = "node_3d"` with box collider
3. Add custom properties:
   ```ini
   [node: streaming_volume_a]
   subscene_path = "res://scenes/chunk_a.uzscene"
   ```

### Script-Based Loading Example

```lua
-- In master scene script (level_01.lua)

local loaded_chunks = {}

function on_start()
    ctx.log:info("Level streaming initialized")

    -- Start monitoring streaming volumes
    local player_node = ctx.scene:find_node_by_name("Player")

    ctx.coroutine.launch(streaming_update_loop, player_node)
end

function streaming_update_loop(player_node)
    while true do
        local player_pos = player_node.position

        -- Check all streaming volumes
        for zone_id, zone_config in ipairs(get_streaming_zones()) do
            if is_player_in_zone(player_pos, zone_config) then
                -- Load if not already loaded
                if not loaded_chunks[zone_id] then
                    ctx.log:info("Player entering zone " .. zone_id)
                    load_chunk_async(zone_id, zone_config)
                end
            else
                -- Unload if player left
                if loaded_chunks[zone_id] then
                    ctx.log:info("Player exiting zone " .. zone_id)
                    unload_chunk(zone_id)
                end
            end
        end

        -- Check every 100ms
        ctx.coroutine.schedule_after(100, function()
            coroutine.resume(coroutine.running())
        end)
        coroutine.yield()
    end
end

function load_chunk_async(zone_id, zone_config)
    -- Load subscene asynchronously
    local handle = ctx.assets:load_async(zone_config.path, "high")

    -- Wait for load in background coroutine
    ctx.coroutine.launch(function()
        ctx.coroutine.wait_until(handle)

        if handle:is_ready() then
            local subscene_root = handle:get()
            subscene_root.active = true
            loaded_chunks[zone_id] = subscene_root
            ctx.log:info("Zone " .. zone_id .. " loaded")
        else
            ctx.log:error("Failed to load zone " .. zone_id .. ": " .. handle:error())
        end
    end)
end

function unload_chunk(zone_id)
    local chunk_root = loaded_chunks[zone_id]
    if chunk_root then
        chunk_root.active = false  -- Hide
        chunk_root = nil  -- Trigger unload
        loaded_chunks[zone_id] = nil
    end
end

function get_streaming_zones()
    return {
        { id = 1, path = "res://scenes/chunk_a.uzscene", center = Vec3(150, 0, 0), radius = 60 },
        { id = 2, path = "res://scenes/chunk_b.uzscene", center = Vec3(250, 0, 0), radius = 60 },
        { id = 3, path = "res://scenes/chunk_c.uzscene", center = Vec3(350, 0, 0), radius = 60 },
        { id = 4, path = "res://scenes/chunk_d.uzscene", center = Vec3(450, 0, 0), radius = 60 },
    }
end

function is_player_in_zone(player_pos, zone)
    local dist = (player_pos - zone.center):length()
    return dist < zone.radius
end
```

### Subscene Management

When a subscene loads, it becomes a subtree in the scene graph. You can:

```lua
-- Get root node of loaded subscene
local chunk_root = loaded_chunks[zone_id]

-- Show/hide without unloading
chunk_root.active = true   -- Visible
chunk_root.active = false  -- Hidden (still in memory)

-- Access nodes in subscene
local npc = chunk_root:find_child_recursive("NPC_Guard")
if npc then
    npc.position = Vec3(10, 0, 5)
end

-- Unload (destroys all nodes in subscene)
chunk_root:delete()  -- Or set to nil
```

---

## Asset Budgets

### Set Memory Limits

Memory budgets prevent your game from using excessive VRAM or RAM.

```lua
-- In game initialization (game.dll startup or level start)

function init_streaming_budget()
    -- Desktop: generous budget
    if ctx.platform == "windows" then
        ctx.streaming:set_memory_budget(1500, 2000)  -- MB VRAM, MB RAM

    -- Console: tight budget
    elseif ctx.platform == "ps5" then
        ctx.streaming:set_memory_budget(1000, 800)

    -- Mobile: very tight
    elseif ctx.platform == "mobile" then
        ctx.streaming:set_memory_budget(300, 500)
    end
end
```

### Monitor Memory Usage

```lua
function update_memory_display()
    local usage = ctx.streaming:memory_usage()

    -- usage = { vram_mb = 800, ram_mb = 1200, vram_limit_mb = 1500, ram_limit_mb = 2000 }

    local vram_pct = (usage.vram_mb / usage.vram_limit_mb) * 100
    local ram_pct = (usage.ram_mb / usage.ram_limit_mb) * 100

    ctx.log:info(string.format(
        "Memory: VRAM %.1f%% (%dMB/%dMB), RAM %.1f%% (%dMB/%dMB)",
        vram_pct, usage.vram_mb, usage.vram_limit_mb,
        ram_pct, usage.ram_mb, usage.ram_limit_mb
    ))

    -- Warn if near budget
    if vram_pct > 90 then
        ctx.log:warning("VRAM near limit!")
    end
    if ram_pct > 90 then
        ctx.log:warning("RAM near limit!")
    end
end
```

### Eviction Strategy

When memory budget is exceeded, the engine automatically evicts unused assets. The algorithm is:

1. **Find candidates**: Assets not currently referenced
2. **Sort by priority**: Lower priority assets evicted first (see next section)
3. **Sort by age**: Least recently used evicted first
4. **Evict until budget OK**: Stop when target memory is freed

Example:
```
VRAM Budget: 1500 MB
Current Usage: 1480 MB
New Load: 50 MB (exceeds budget)

Eviction Candidates (sorted):
- res://meshes/tree_04.gltf (priority: 10, last_used: 45s ago)  ← Evict this (100MB)
- res://textures/sky_night.png (priority: 15, last_used: 60s ago)
- res://audio/ambient_loop.wav (priority: 20, last_used: 120s ago)

After eviction:
- Free 100MB
- Usage: 1380 MB
- New load proceeds
- Final usage: 1430 MB (OK)
```

---

## Preloading Strategy

### Identify Critical Assets

Before each major scene transition, preload assets the player will immediately need:

```lua
function on_level_start()
    -- Preload assets for next 30 seconds of gameplay
    ctx.assets:preload({
        "res://meshes/player_sword.gltf",
        "res://meshes/player_shield.gltf",
        "res://animations/player_idle.uzanim",
        "res://animations/player_walk.uzanim",
        "res://audio/events/footstep.uzevent",
        "res://audio/events/sword_swing.uzevent",
    }, "high")

    ctx.log:info("Critical assets preloaded")
end

function on_approaching_boss_arena()
    -- Boss fight coming up, preload boss assets
    ctx.assets:preload({
        "res://meshes/boss_dragon.gltf",
        "res://animations/boss_attack_1.uzanim",
        "res://animations/boss_attack_2.uzanim",
        "res://audio/events/boss_roar.uzevent",
        "res://audio/boss_theme.ogg",
    }, "high")
end
```

### Background Preload of Optional Assets

Load expensive assets with low priority while player is busy:

```lua
function on_player_idle(duration_seconds)
    if duration_seconds > 5 then
        -- Player hasn't moved in 5 seconds, safe to load optional content
        ctx.assets:preload({
            "res://meshes/detailed_character_model.gltf",
            "res://textures/high_res_environment.png",
        }, "low")

        ctx.log:info("Preloading optional high-detail assets")
    end
end
```

### Unload Unused Assets

Explicitly free memory when you know assets won't be used:

```lua
function exit_level(level_name)
    -- Clear all assets except core engine assets
    ctx.streaming:garbage_collect()

    ctx.log:info("Unused assets evicted, memory freed")
end
```

---

## Debug & Profiling

### Streaming Overlay

Enable the on-screen debug overlay to watch loading in real-time:

```lua
-- Enable overlay
ctx.streaming:set_debug_overlay(true)

-- Disable overlay
ctx.streaming:set_debug_overlay(false)
```

**On-screen display shows:**
```
Streaming Debug Overlay
─────────────────────────
Pending: 2 | Loading: 3 | Ready: 148
VRAM: 800MB / 1500MB (53%)
RAM:  1200MB / 2000MB (60%)

Top 5 In-Flight:
1. res://meshes/character.gltf (est 500ms)
2. res://textures/terrain_albedo.png (est 200ms)
3. res://meshes/building_a.gltf (est 800ms)
4. res://audio/ambient_loop.wav (est 150ms)
5. res://meshes/tree_forest.gltf (est 300ms)

Recent Evictions (LRU):
- res://meshes/tree_04.gltf (priority 10)
- res://textures/sky_night.png (priority 5)
```

### Get Streaming Statistics

```lua
function log_streaming_stats()
    local stats = ctx.streaming:debug_info()

    -- stats.pending_requests = 2
    -- stats.in_progress_requests = 3
    -- stats.completed_requests = 142
    -- stats.average_load_time_ms = 150
    -- stats.bandwidth_mbps = 45.2
    -- stats.evicted_assets = 3

    ctx.log:info(string.format(
        "Streaming: %d pending, %d loading, %d ready | Avg latency: %.0fms | BW: %.1f MB/s",
        stats.pending_requests,
        stats.in_progress_requests,
        stats.completed_requests,
        stats.average_load_time_ms,
        stats.bandwidth_mbps
    ))
end
```

### Profile Individual Handles

```lua
function debug_handle(handle, name)
    local info = handle:debug_info()

    ctx.log:info(string.format(
        "%s: state=%s size=%dMB started=%dms ago",
        name,
        info.state,
        info.size_bytes / (1024 * 1024),
        os.time() * 1000 - info.started_ms
    ))
end

-- Usage
local mesh_handle = ctx.assets:load_async("res://meshes/character.gltf", "high")
debug_handle(mesh_handle, "Character Mesh")
```

### IPC Debug Commands (from Editor)

```
# Get current streaming status
$ uz ipc streaming.get_status
→ {pending: 2, loading: 3, ready: 148, memory_usage: {...}}

# List all in-flight requests
$ uz ipc streaming.get_requests
→ [{path: "res://meshes/...", priority: 255, elapsed_ms: 150}, ...]

# Cancel a specific request
$ uz ipc streaming.cancel_handle 12345

# Force garbage collection
$ uz ipc streaming.garbage_collect

# Simulate memory pressure (for testing)
$ uz ipc streaming.simulate_memory_pressure 0.9
```

---

## Best Practices

### 1. Use Coroutines for Async Waits

```lua
-- GOOD: Non-blocking
function load_weapon()
    local handle = ctx.assets:load_async("res://meshes/sword.gltf", "high")
    ctx.coroutine.wait_until(handle)
    local mesh = handle:get()
    return mesh
end

-- BAD: Blocks game thread
function load_weapon_wrong()
    local handle = ctx.assets:load_async("res://meshes/sword.gltf", "high")
    handle:wait()  -- BLOCKS for 500ms!
    return handle:get()
end
```

### 2. Prioritize Critical Assets

```lua
-- Critical (player needs immediately)
ctx.assets:load_async("res://meshes/player.gltf", "high")

-- Normal (gameplay assets)
ctx.assets:load_async("res://meshes/enemy_01.gltf", "normal")

-- Low (background, optional)
ctx.assets:load_async("res://textures/skybox_night.png", "low")
```

### 3. Batch Preloads

```lua
-- GOOD: Batch preload
ctx.assets:preload({
    "res://meshes/a.gltf",
    "res://meshes/b.gltf",
    "res://textures/c.png",
}, "high")

-- OKAY but less efficient: One at a time
ctx.assets:load_async("res://meshes/a.gltf", "high")
ctx.assets:load_async("res://meshes/b.gltf", "high")
ctx.assets:load_async("res://textures/c.png", "high")
```

### 4. Set Budgets Based on Platform

```lua
function init_memory_budget()
    local info = ctx.system:platform_info()

    if info.platform == "pc" and info.vram_mb >= 4000 then
        -- High-end PC: plenty of VRAM
        ctx.streaming:set_memory_budget(2000, 3000)
    elseif info.platform == "console" then
        -- PS5: shared memory
        ctx.streaming:set_memory_budget(1000, 1000)
    else
        -- Mobile: very limited
        ctx.streaming:set_memory_budget(256, 512)
    end
end
```

### 5. Handle Load Failures Gracefully

```lua
function equip_item(item_mesh_path)
    local handle = ctx.assets:load_async(item_mesh_path, "normal")

    ctx.coroutine.launch(function()
        if not ctx.coroutine.wait_until(handle) then
            ctx.log:error("Failed to load item: " .. handle:error())
            use_placeholder_mesh()
            return
        end

        local mesh = handle:get()
        apply_mesh_to_character(mesh)
    end)
end

function use_placeholder_mesh()
    -- Show error indicator or fallback mesh
    self.node.mesh = ctx.assets:get("res://meshes/placeholder.gltf")
end
```

### 6. Monitor Streaming in Development

Always run with the streaming overlay enabled during development to catch:
- Assets loading unexpectedly
- Memory leaks (assets not evicting)
- Priority inversions (low-priority loading before high-priority)

```lua
if ctx.is_editor_mode() then
    ctx.streaming:set_debug_overlay(true)
end
```

---

## Common Patterns

### Pattern 1: Load & Equip Weapon

```lua
function equip_sword_async()
    ctx.coroutine.launch(function()
        local handle = ctx.assets:load_async("res://meshes/sword.gltf", "high")

        ctx.log:info("Loading sword...")
        ctx.coroutine.wait_until(handle)

        if not handle:is_ready() then
            ctx.log:error("Sword load failed")
            return
        end

        local mesh = handle:get()
        self.node.mesh = mesh
        ctx.log:info("Sword equipped!")
    end)
end
```

### Pattern 2: Stream Terrain Chunks

```lua
function stream_terrain()
    local loaded_chunks = {}

    ctx.coroutine.launch(function()
        while true do
            local player_pos = ctx.scene:find_node_by_name("Player").position

            -- Load chunks within streaming distance
            for chunk_id, chunk_info in ipairs(get_all_terrain_chunks()) do
                local dist = (player_pos - chunk_info.center):length()

                if dist < 200 and not loaded_chunks[chunk_id] then
                    -- Load this chunk
                    load_terrain_chunk(chunk_id, chunk_info)
                    loaded_chunks[chunk_id] = true
                elseif dist > 300 and loaded_chunks[chunk_id] then
                    -- Unload distant chunk
                    unload_terrain_chunk(chunk_id)
                    loaded_chunks[chunk_id] = false
                end
            end

            ctx.coroutine.schedule_after(500, function()
                coroutine.resume(coroutine.running())
            end)
            coroutine.yield()
        end
    end)
end

function load_terrain_chunk(chunk_id, info)
    local handle = ctx.assets:load_async(info.path, "normal")

    ctx.coroutine.launch(function()
        ctx.coroutine.wait_until(handle)
        if handle:is_ready() then
            local terrain_root = handle:get()
            terrain_root.position = info.center
            terrain_root.active = true
        end
    end)
end
```

### Pattern 3: Preload Before Transition

```lua
function load_next_level(level_name)
    ctx.log:info("Loading " .. level_name)

    -- Disable input during load
    ctx.input:set_enabled(false)

    -- Show loading screen
    show_loading_screen()

    -- Preload critical assets for next level
    ctx.assets:preload(get_level_critical_assets(level_name), "high")

    -- Wait for all preloads
    ctx.streaming:flush()

    ctx.log:info("All assets ready, starting " .. level_name)

    -- Load actual level scene
    ctx.scene:load("res://scenes/" .. level_name .. ".uzscene")

    -- Hide loading screen
    hide_loading_screen()
    ctx.input:set_enabled(true)
end
```

---

## Troubleshooting

### Problem: Assets never finish loading

**Cause**: File not found or permission error

**Debug**:
```lua
local handle = ctx.assets:load_async("res://meshes/nonexistent.gltf", "high")
ctx.coroutine.wait_until(handle)

if not handle:is_ready() then
    print("Error: " .. handle:error())  -- Prints actual error
end
```

**Fix**: Check file path and permissions.

### Problem: Memory usage keeps growing

**Cause**: Assets not being evicted, or handles held indefinitely

**Debug**:
```lua
ctx.streaming:set_debug_overlay(true)
-- Watch "Recent Evictions" section - should show activity

-- Check if you're holding handles:
print(handle:is_valid())  -- false if evicted
```

**Fix**:
- Release handles when done: `handle = nil`
- Call `ctx.streaming:garbage_collect()` between scenes
- Lower memory budget if intentional eviction needed

### Problem: Frame stutters during load

**Cause**: Asset too large or priority too low

**Debug**:
```lua
local stats = ctx.streaming:debug_info()
print("Bandwidth: " .. stats.bandwidth_mbps .. " MB/s")  -- Check I/O speed
print("Avg load time: " .. stats.average_load_time_ms .. "ms")
```

**Fix**:
- Increase priority: `load_async(path, "high")` instead of "low"
- Use smaller asset versions (precompressed, lower-quality)
- Preload earlier (before visible need)

### Problem: Placeholder assets visible (magenta checkerboard, gray boxes)

**Cause**: Asset still loading

**Debug**:
- Enable streaming overlay: `ctx.streaming:set_debug_overlay(true)`
- Check "In-Flight" section for this asset
- If stuck, check filesystem or network (if streaming from server)

**Fix**:
- Asset is loading, placeholder is temporary (expected behavior)
- If it gets stuck, cancel and retry: `handle:cancel()`

### Problem: Eviction happening too aggressively

**Cause**: Memory budget too low for scene

**Debug**:
```lua
local usage = ctx.streaming:memory_usage()
print(string.format("VRAM: %d/%d MB", usage.vram_mb, usage.vram_limit_mb))
```

**Fix**:
```lua
-- Increase budget
ctx.streaming:set_memory_budget(2000, 3000)  -- More MB available
```

---

## Summary

Use async streaming to:
- Load large worlds without stutters
- Manage memory budgets automatically
- Show progress and debug information
- Stream level chunks as player moves

Key APIs:
- `ctx.assets:load_async(path, priority)` — Load without blocking
- `ctx.coroutine.wait_until(handle)` — Pause Lua until ready
- `ctx.streaming:set_memory_budget(vram, ram)` — Set limits
- `ctx.streaming:debug_info()` — Get statistics

See [async-streaming.md](../specs/async-streaming.md) for full API reference.
