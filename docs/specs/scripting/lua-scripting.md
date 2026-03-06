# Spec: Lua Scripting System

> **Status**: Planned (Phase 1)
> **Runtime**: LuaJIT 2.1 (JIT + interpreter)
> **VM owner**: `game.dll` (not `render.exe`)
> **Authoring**: Plain `.lua` files under `res://`

---

## Overview

Lua is a **complementary** scripting layer on top of C++. `game.dll` remains required — it owns the LuaJIT VM, registers the engine bindings, and drives all Lua scripts. Individual scene nodes can attach `.lua` scripts alongside `.cpp` scripts.

This gives the engine three authoring speeds:

| Layer | File | Speed | Use Case |
|-------|------|-------|----------|
| Engine C++ | `src/engine/`, `src/renderer/` | Slow (full rebuild) | Core systems, rendering, physics |
| Game C++ | `game.dll` (`game.cpp`) | Fast (~2.5s hot-reload) | Complex game systems, state machines, physics callbacks |
| Lua script | `res://game/*.lua` | Instant (~60ms hot-reload) | NPC behavior, pickups, triggers, UI events, prototyping |

Lua does not replace C++. It extends it.

---

## Architecture

```
render.exe
│  knows nothing about Lua
│  C ABI boundary (game_api_t) unchanged
│
game.dll  (C++, links LuaJIT as static lib)
│  ├── LuaVM lifecycle: luajit_newstate() / lua_close()
│  ├── Binding registration: exposes engine API as Lua globals
│  ├── Script runner: dispatches on_start/on_update/on_stop per node
│  └── Hot-reload manager: detects .lua changes, reloads modules
│
src/engine/scripting/  (C++ binding helpers, compiled into uz_engine.lib)
│  ├── lua_binding.h/cpp   — Vec3, Transform, Scene, ECS, InputState metatables
│  ├── script_runner.h/cpp — per-node Lua dispatch, pcall wrapper
│  └── lua_sandbox.h/cpp   — strips dangerous globals from VM
│
LuaJIT VM  (one per active scene, reset on scene load)
│  sandboxed: no io.*, os.*, require(), filesystem
│  each .lua file = one Lua module (table with lifecycle methods)
│  each node instance = one copy of module table (metatable __index)
│
res://game/
   player.lua
   enemy_ai.lua
   door_trigger.lua
   ...
```

### Why LuaJIT Lives Inside `game.dll`

The stable C ABI boundary (`game_api_t`) between `render.exe` and `game.dll` must never be crossed by Lua types. Placing LuaJIT inside `game.dll` means:

- `render.exe` never includes `<lua.h>` — zero coupling
- `lua_State*` never crosses the DLL boundary
- When `game.dll` hot-reloads, LuaJIT is torn down and rebuilt — all Lua state is cleanly reset (or preserved via `on_save_state`)
- Build tool can strip LuaJIT to interpreter-only for shipping without touching the engine

---

## Script Attachment in `.uzscene`

Script type is detected by file extension:

```ini
[uzscene version=1 name="Level01"]

[node id=1 type=Node3D name="Root" parent=0]

; Lua script — loaded into LuaJIT VM
[node id=5 type=Node3D name="Enemy" parent=1]
position = [10.0, 0.0, 0.0]
script   = "res://game/enemy_ai.lua"

; C++ script — handled by game.dll create_script (Phase 3)
[node id=6 type=Node3D name="Player" parent=1]
script   = "res://game/player.cpp"

; No script — node has no logic
[node id=7 type=StaticMesh name="Crate" parent=1]
mesh     = "res://assets/crate.uzmesh"
```

Graceful degradation applies equally to Lua:
- Script file not found → `[lua] script not found: res://game/enemy_ai.lua — node 'Enemy' runs without logic`
- Lua parse error on load → logged, node runs without logic (no crash)
- Runtime error in `on_update` → caught by pcall, logged with file+line, script disabled for that node

---

## Script Lifecycle

Each `.lua` file defines a module (Lua table) with optional lifecycle methods:

```lua
-- res://game/enemy_ai.lua
local M = {}

-- Called once when the scene loads (or when the script hot-reloads)
-- ctx: GameContext (read/write access to scene, ECS, resources)
-- self: per-instance table (unique to this node)
function M:on_start(ctx)
    self.speed     = 4.0
    self.health    = 100
    self.target    = ctx.scene:find_node("Player")
    self.nav_timer = 0.0
end

-- Called every frame on the Game Logic thread
-- dt: frame delta in seconds (same as ctx.delta_time)
function M:on_update(ctx, dt)
    if not self.target then return end

    self.nav_timer = self.nav_timer + dt
    if self.nav_timer >= 0.2 then
        self.nav_timer = 0.0
        local dir = (self.target.position - self.position):normalized()
        self.position = self.position + dir * self.speed * dt
    end
end

-- Called when the scene unloads or script is removed from node
function M:on_stop(ctx)
    -- cleanup: release resources, unsubscribe events
end

return M
```

Rules:
- All three methods are optional (empty `M:on_stop` is never called if not defined)
- `self` is the per-node instance table — unique per node, persists across frames
- Module table `M` is shared (prototype); instances use `M` as `__index` metatable
- Mutations to `self` are safe; mutations to `M` affect all instances of that script

---

## Per-Node Instance Model

One `.lua` file → one module `M`. Multiple nodes can attach the same script. Each node gets its own instance table:

```
M (prototype, loaded once)
  on_start, on_update, on_stop functions

node 5 "Enemy1" → instance5 = setmetatable({}, {__index = M})
node 8 "Enemy2" → instance8 = setmetatable({}, {__index = M})
node 12 "Enemy3" → instance12 = setmetatable({}, {__index = M})
```

Each instance has its own `self.speed`, `self.health`, etc. They share the function definitions from `M`.

This is managed by `script_runner.cpp` — `game.dll` never exposes this to Lua developers, it just works.

---

## Engine API (Lua)

The following API is injected into the VM by `game.dll` via `lua_binding.cpp`:

### `ctx` — GameContext

| Lua | Type | Description |
|-----|------|-------------|
| `ctx.delta_time` | `float` | Frame delta (seconds) |
| `ctx.time_elapsed` | `float` | Total time since game start (seconds) |
| `ctx.frame_index` | `integer` | Monotonic frame counter |
| `ctx:log(msg)` | `string → nil` | Log to engine console (level: info) |
| `ctx:log_warn(msg)` | `string → nil` | Log warning |
| `ctx:log_error(msg)` | `string → nil` | Log error |
| `ctx:load_mesh(path)` | `string → MeshHandle` | Load/cache a mesh |
| `ctx:load_mat(path)` | `string → MatHandle` | Load/cache a material |
| `ctx.scene` | `Scene` | Active scene (see below) |
| `ctx.input` | `InputState` | Current frame input (see below) |

### `ctx.scene` — Scene Access

| Lua | Description |
|-----|-------------|
| `ctx.scene:find_node(name)` | Returns `NodeRef` by name, or `nil` |
| `ctx.scene:find_node_by_id(id)` | Returns `NodeRef` by node ID, or `nil` |
| `ctx.scene:add_node(type, name, parent_ref)` | Enqueues `SceneCommand::ADD_NODE`; returns pending `NodeRef` |
| `ctx.scene:remove_node(node_ref)` | Enqueues `SceneCommand::REMOVE_NODE` |
| `ctx.scene:instantiate(path, parent_ref)` | Instantiate a `.uzscene` prefab as subtree |

Scene mutations are **always enqueued** (applied at frame start on Main thread) — Lua cannot mutate the scene graph directly.

### `NodeRef` — Node Handle

| Lua | Type | Description |
|-----|------|-------------|
| `node.name` | `string` (read-only) | Node name |
| `node.id` | `integer` (read-only) | Stable node ID |
| `node.position` | `Vec3` | World position (read/write) |
| `node.rotation` | `Vec3` | Euler angles degrees (read/write) |
| `node.scale` | `Vec3` | Scale (read/write) |
| `node.enabled` | `boolean` | Enable/disable node |
| `node:get_child(name)` | `string → NodeRef\|nil` | First child with that name |
| `node:get_parent()` | `NodeRef\|nil` | Parent node |
| `node:is_valid()` | `boolean` | False if node was removed |

Writing `node.position` enqueues a `SceneCommand::SET_PROPERTY` — no direct scene mutation.

### `ctx.input` — Input State

| Lua | Description |
|-----|-------------|
| `ctx.input:pressed(action)` | True on first frame action is triggered |
| `ctx.input:released(action)` | True on first frame action is released |
| `ctx.input:held(action)` | True while action is held |
| `ctx.input:axis(action)` | Float value for analog axis (-1 to 1) |
| `ctx.input:mouse_delta()` | Returns `Vec2` mouse delta this frame |

`action` is a `StringId` — pass the action name as a string: `ctx.input:pressed("Jump")`.

### `Vec3` — Math Type

```lua
local v1 = Vec3(1, 0, 0)
local v2 = Vec3(0, 1, 0)

-- Arithmetic
local sum  = v1 + v2          -- Vec3(1, 1, 0)
local diff = v1 - v2          -- Vec3(1, -1, 0)
local scl  = v1 * 2.0         -- Vec3(2, 0, 0)

-- Methods
local len  = v1:length()      -- float
local norm = v1:normalized()  -- Vec3
local dot  = v1:dot(v2)       -- float
local cross = v1:cross(v2)    -- Vec3
local lerped = Vec3.lerp(v1, v2, 0.5)  -- Vec3

-- Field access
local x = v1.x
local y = v1.y
local z = v1.z
```

### `Quat` — Quaternion

```lua
local q1 = Quat.identity()
local q2 = Quat.from_euler(0, 45, 0)   -- degrees YXZ
local q3 = Quat.from_axis_angle(Vec3(0,1,0), math.rad(45))

local combined = q1 * q2               -- multiply quaternions
local rotated  = q2:rotate(Vec3(1,0,0)) -- rotate a vector
local slerped  = Quat.slerp(q1, q2, t)
```

---

## Error Handling

All Lua calls from C++ use `lua_pcall`. Errors do not crash the engine:

```
[lua error] res://game/enemy_ai.lua:14: attempt to index nil value (field 'target')
  stack traceback:
    res://game/enemy_ai.lua:14: in method 'on_update'
    [C]: in ?
```

On error:
1. Error message logged to engine console (level: error)
2. Script disabled for the affected node this frame
3. Script retried next `on_start` (e.g., after hot-reload or scene reload)
4. If `UZ_EDITOR=1`, the error is sent to VSCode as a diagnostic annotation on the Lua file

---

## Hot-Reload

`.lua` files are watched by `FileWatcher`. When a file changes:

```
t=0ms    Developer saves enemy_ai.lua
t=50ms   FileWatcher detects change (ReadDirectoryChangesW)
t=50ms   LuaScriptReloader queues reload for "res://game/enemy_ai.lua"
t=55ms   Main thread processes queue (frame start):
           For each node using this script:
             call M:on_stop(ctx)        (cleanup)
           luaL_loadfile(L, new_path)   (reload module table)
           lua_setglobal(L, module_key) (replace M in VM)
           For each node using this script:
             call M:on_start(ctx)       (reinitialize)
t=60ms   Next on_update uses new code
```

**Total: ~60ms** (vs ~2500ms for C++ DLL reload).

Per-node `self` table is preserved across Lua hot-reload (only functions change). `on_stop` is called so scripts can release external resources; `on_start` is called again to reinitialize script-specific state.

### Lua State Across game.dll Hot-Reload

When `game.dll` itself hot-reloads (because C++ changed), the LuaJIT VM is destroyed and recreated. Lua script state must be preserved via `on_save_state` / `on_load_state`:

```cpp
// game.cpp — in uz_game_save_state
void uz_game_save_state(GameContext* ctx, Arena* out) {
    // Serialize Lua VM state to blob
    lua_vm.serialize_all_instances(out);  // writes all self tables as msgpack
}

void uz_game_load_state(GameContext* ctx, void* blob, size_t blob_size) {
    if (!blob) return;
    lua_vm.deserialize_all_instances(blob, blob_size);  // restores self tables
}
```

Only `self` table values that are serializable (numbers, strings, booleans, nested tables of those) are preserved. Node references (`NodeRef`) are re-resolved by name after reload.

---

## Sandboxing

The LuaJIT VM is initialized with a restricted global environment:

### Stripped (forbidden in game scripts)

| Global | Reason |
|--------|--------|
| `io.*` | Direct filesystem access — use `ctx:load_mesh()` etc. |
| `os.*` | Process control, time manipulation |
| `package.*` | Module loading from arbitrary paths |
| `require` | Module loading — scripts use VM-internal modules only |
| `dofile`, `loadfile` | Arbitrary file execution |
| `load`, `loadstring` | Dynamic code execution (security) |
| `debug.*` | Stripped in non-debug builds; allowed in `UZ_DEBUG=1` |

### Allowed

| Global | Notes |
|--------|-------|
| `math.*` | Full Lua math library |
| `string.*` | Full Lua string library |
| `table.*` | Full Lua table library |
| `pcall`, `xpcall` | Error-safe calls |
| `pairs`, `ipairs`, `next` | Table iteration |
| `select`, `unpack` | Vararg utilities |
| `type`, `tostring`, `tonumber` | Type utilities |
| `setmetatable`, `getmetatable` | OOP support |
| `rawget`, `rawset`, `rawequal` | Metatable bypass |
| `Vec3`, `Quat` | Engine math types |
| `ctx` | Injected per-call (not a global) |

---

## LuaJIT FFI (Advanced)

LuaJIT's FFI allows Lua code to call C functions and access C structs directly, without binding overhead. This is **only available in `UZ_DEBUG=1` or `UZ_DEV=1`** builds — not in shipping.

```lua
-- Only in dev/debug builds (FFI not available in UZ_SHIPPING=1)
local ffi = require("ffi")

ffi.cdef [[
    typedef struct { float x, y, z; } Vec3f;
    void log_info(const char* msg);
]]

-- Direct struct access — zero overhead
local pos = ffi.new("Vec3f", 1.0, 2.0, 3.0)
ffi.C.log_info("hello from ffi")
```

In shipping builds, `require("ffi")` returns `nil` (stripped). Scripts must check:

```lua
local ffi = require("ffi")
if not ffi then
    -- production path: use engine API instead
end
```

---

## Build / Shipping Configuration

| Build Mode | LuaJIT Mode | Scripts | Hot-reload |
|-----------|------------|---------|-----------|
| `--editor` (`UZ_EDITOR=1`) | JIT + debug info | From `res://` filesystem | Enabled |
| `--play` (`UZ_DEV=1`) | JIT, no debug info | From `res://` filesystem | Disabled |
| Shipping (`UZ_SHIPPING=1`) | Interpreter-only | From `.uzpak` (pre-compiled bytecode) | Disabled |

**LuaJIT binary size**:
- Full JIT: ~800 KB static lib
- Interpreter-only (`LUAJIT_DISABLE_JIT`): ~200 KB static lib

Shipping uses `luajit -b` to pre-compile all `.lua` files to LuaJIT bytecode (`.luac`) during the Cook stage. Bytecode is packed into `.uzpak`. This:
- Removes all source code from the shipped binary
- Skips parsing at game start (faster boot)
- Gives a minor performance benefit (no JIT warm-up needed for simple scripts)

---

## C++ Source Structure

```
src/engine/scripting/
├── lua_binding.h/cpp       — register Vec3, Quat, NodeRef, Scene, InputState metatables
│                              called by game.dll on VM init
├── script_runner.h/cpp     — per-node dispatch: on_start / on_update / on_stop
│                              pcall wrapper, error reporting
└── lua_sandbox.h/cpp       — strip dangerous globals from lua_State
                               called once after luajit_newstate()
```

These files are compiled into `uz_engine.lib`. LuaJIT itself (`libluajit.lib`) is linked only into `game.dll` — never into `render.exe`.

---

## AI Usage Guide

Lua scripts are ideal for AI-assisted development because:

- Plain text, simple syntax, no compilation step
- Hot-reload in ~60ms — AI can iterate rapidly
- Error messages include file + line number (Claude can fix them immediately)
- No type system friction — prototype quickly, optimize in C++ later

### Typical AI Workflow

```
User: "add an enemy that patrols between two waypoints"

Claude:
  1. Creates res://game/patrol_enemy.lua
  2. Adds script = "res://game/patrol_enemy.lua" to relevant node in .uzscene
  3. Tests via IPC: engine.screenshot to verify
  4. Iterates based on user feedback
```

### Pattern: Data from `.uzscene`, logic in `.lua`

```ini
; .uzscene — data visible to non-programmers
[node id=10 type=Node3D name="PatrolEnemy" parent=1]
script        = "res://game/patrol_enemy.lua"
patrol_speed  = 3.5
patrol_radius = 8.0
```

```lua
-- patrol_enemy.lua — reads custom properties from node
function M:on_start(ctx)
    -- custom properties on the node are accessible as fields
    self.speed  = self.patrol_speed  or 3.0
    self.radius = self.patrol_radius or 6.0
    self.angle  = 0.0
    self.origin = Vec3(self.position.x, self.position.y, self.position.z)
end

function M:on_update(ctx, dt)
    self.angle = self.angle + self.speed * dt
    self.position = self.origin + Vec3(
        math.cos(self.angle) * self.radius,
        0,
        math.sin(self.angle) * self.radius
    )
end
```

---

## Physics Queries

> **Available**: Phase 3+ (Jolt Physics initialized). All functions return empty/false results before physics is active.

Physics queries are accessible through `ctx.world` — a world-space physics query interface injected by `game.dll` when the physics subsystem is running.

### `ctx.world:raycast(origin, direction, max_dist [, layer_mask])` → result table

Cast a ray from `origin` along `direction` up to `max_dist` units. Returns a result table.

```lua
local hit = ctx.world:raycast(
    self.position,                      -- origin: Vec3
    Vec3(0, -1, 0),                     -- direction: Vec3 (normalized)
    10.0                                -- max_dist: float
)

if hit.hit then
    ctx:log("Hit " .. hit.node:name() .. " at distance " .. hit.distance)
    -- Draw a debug mark at hit.point (Vec3)
end
```

Return table fields:

| Field | Type | Description |
|-------|------|-------------|
| `hit` | `boolean` | `true` if anything was hit |
| `point` | `Vec3` | World-space hit position (zero vector if no hit) |
| `normal` | `Vec3` | Surface normal at hit point (zero vector if no hit) |
| `node` | `NodeRef\|nil` | The node that was hit, or `nil` |
| `distance` | `float` | Distance from origin to hit point (0 if no hit) |

Optional `layer_mask` is an integer bitmask of physics layers to include. If omitted, all layers are queried.

```lua
-- Only hit nodes on layers 1 and 3
local hit = ctx.world:raycast(origin, dir, 50.0, 0x5)
```

### `ctx.world:linecast(from, to [, layer_mask])` → result table

Cast a ray from `from` to `to`. Same return table as `raycast`. Convenience wrapper — internally computes direction and max_dist.

```lua
local hit = ctx.world:linecast(self.position, target.position)
if not hit.hit then
    -- Clear line of sight
    self:attack(ctx, target)
end
```

### `ctx.world:overlap_sphere(center, radius [, layer_mask])` → `NodeRef[]`

Returns all nodes whose physics shapes overlap a sphere of the given `radius` at `center`.

```lua
-- Explosion radius check
local victims = ctx.world:overlap_sphere(explosion_pos, 5.0)
for _, node in ipairs(victims) do
    if node:has_tag("damageable") then
        deal_damage(node, 50)
    end
end
```

- Returns a Lua array (may be empty `{}`)
- Order is unspecified (depends on Jolt internal broadphase)
- The querying node itself may be included — filter by `node.id ~= self.id` if needed

### `ctx.world:overlap_box(center, half_extents [, layer_mask])` → `NodeRef[]`

Returns all nodes whose physics shapes overlap an axis-aligned box.

```lua
-- Check for collectibles in a pickup volume
local items = ctx.world:overlap_box(
    self.position,
    Vec3(1.0, 0.5, 1.0)   -- half_extents: box is 2×1×2 units
)
for _, item in ipairs(items) do
    if item:has_tag("pickup") then
        collect_item(ctx, item)
    end
end
```

### Before Physics Is Initialized (Phase 1–2)

When called before Jolt Physics is active, all query functions return safe empty results:

- `raycast` / `linecast`: returns `{hit=false, point=Vec3(0,0,0), normal=Vec3(0,0,0), node=nil, distance=0}`
- `overlap_sphere` / `overlap_box`: returns `{}`

Scripts do not need to guard against nil returns — the API is always safe to call.

---

## IPC Handlers (`lua.*`)

These handlers are available in `--editor` mode for tooling and AI inspection:

| Command | Args | Returns | Purpose |
|---------|------|---------|---------|
| `lua.eval` | `{code: string}` | `{result: any, error: string}` | Execute a Lua snippet in the VM |
| `lua.get_globals` | — | `{globals: table}` | List all non-engine globals (for inspector) |
| `lua.reload_script` | `{path: string}` | `{ok: bool}` | Force reload a specific .lua file |
| `lua.get_errors` | — | `[{path, line, msg}]` | Pending Lua errors (for diagnostics) |

`lua.eval` is editor-only and stripped in shipping. It enables the MCP tool to run arbitrary Lua in the live game scene for debugging and AI-driven scene manipulation.

---

## CScriptRef — ECS Component (Updated)

```cpp
// ecs/components.h
enum class ScriptKind : uint8_t { CPP, LUA };

struct CScriptRef {
    StringId    script_id;     // FNV-1a hash of res:// path
    ScriptKind  kind;          // CPP or LUA
    void*       state;         // CPP: opaque game.dll state ptr
                               // LUA: lua_State* owned by LuaVM (weak ref)
};
```

`script_id` is used by `LuaScriptReloader` to find all nodes that need refreshing when a `.lua` file changes.

---

## Phase Roadmap

| Phase | Deliverable |
|-------|-------------|
| **Phase 1** | `src/engine/scripting/` binding helpers, LuaJIT vcpkg integration, `lua.eval` IPC handler, `.lua` file hot-reload, error → VSCode diagnostics |
| **Phase 2** | Custom node properties readable from Lua (`patrol_speed`, etc.), `ctx.scene:instantiate()`, `lua.eval` via MCP tool |
| **Phase 3** | Physics callbacks in Lua (`on_collision`, `on_trigger`), animation event callbacks (`on_anim_notify`), LuaJIT FFI available in dev builds |
| **Phase 4** | Pre-compiled Lua bytecode in `.uzpak`, LuaJIT interpreter-only for shipping, `lua.*` IPC stripped |

---

## Coroutines and Async Primitives

Coroutines enable multi-frame sequences without state machines:

```lua
-- Wait N seconds
function on_start(self)
    ctx:start_coroutine(function()
        self:set_color("red")
        ctx.coroutine.wait(2.0)       -- yield for 2 seconds
        self:set_color("green")
        ctx.coroutine.wait_frames(5)  -- yield for 5 frames
        self:set_color("white")
    end)
end
```

**LuaJIT note**: `coroutine.yield` across C call boundaries is NYI in LuaJIT's JIT compiler. Coroutine code runs in interpreter mode (~10x slower than JIT). For scripts with < 1ms per frame budget, this is acceptable. Do not use coroutines in hot paths (per-entity update loops).

Implementation: `lua_resume` is called from `LuaCoroutineScheduler::drain()` in the update loop. Each coroutine is a `lua_State` created via `lua_newthread`.

---

## Module System

`require` is enabled in a controlled form. Modules are resolved relative to `res://scripts/lib/`:

```lua
local mathutil = require("mathutil")     -- loads res://scripts/lib/mathutil.lua
local tween = require("tween.ease")      -- loads res://scripts/lib/tween/ease.lua
```

- `require` from absolute paths (`/`, `C:\\`) is blocked by sandbox
- Modules are cached per VM instance (loaded once, shared across scripts)
- Module search path is configurable in `.uzproj`:
  ```toml
  [scripting]
  module_paths = ["res://scripts/lib", "res://mods/lib"]
  ```

---

## Shared State

Scripts can share data via `ctx.shared`, a Lua table backed by the C++ `GameState` struct:

```lua
-- In player.lua
ctx.shared.player_hp = 100

-- In hud.lua
local hp = ctx.shared.player_hp  -- reads same value
```

`ctx.shared` changes fire `GameStateChangedEvent{key, old_value, new_value}` on the EventBus. Subscribers include the UI binding system and audio snapshot system.

---

## Bytecode Cache

Compiled Lua bytecode is cached to `{cache_dir}/lua/{script_hash}.luac`. On load, if the cache exists and the source file hash matches, the bytecode is loaded directly. Cache invalidation: script file mtime or content hash change.

---

## Rich Error Reporting

Lua errors include:
- File, line number, and error message
- Full stack trace with function names
- Local variable snapshot at each stack frame
- `self` table dump (first 10 keys)

Errors are sent via IPC `lua.get_errors` and surfaced as VSCode diagnostic markers.

---

## Configurable Sandbox

Sandbox level is configurable per context:

| Level | `require` | `io` | `os` | `ffi` | Use Case |
|-------|----------|------|------|-------|----------|
| `strict` | restricted | blocked | blocked | blocked | Game scripts (default) |
| `editor` | unrestricted | read-only | time only | blocked | Editor tool scripts |
| `trusted` | unrestricted | full | full | allowed | Engine plugins |

Set in `.uzproj`:
```toml
[scripting]
sandbox_level = "strict"
```

---

## Lua Test Framework

Scripts can include inline unit tests using `ctx.test`:

```lua
if ctx.test then
    ctx.test.describe("damage_calc", function()
        ctx.test.it("should reduce hp", function()
            local hp = damage_calc(100, 30)
            ctx.test.assert_eq(hp, 70)
        end)
    end)
end
```

Tests run via IPC `lua.run_tests` and results are reported in VSCode.

---

## Related Docs

- [engine/design-decisions.md](../engine/design-decisions.md) — section 2.8: why Lua complements C++
- [engine/hot-reload.md](../engine/hot-reload.md) — Lua hot-reload flow vs C++ DLL reload
- [engine/cpp-architecture.md](../engine/cpp-architecture.md) — `CScriptRef` ECS component, LuaBinding API
- [modules/engine/README.md](../modules/engine/README.md) — `scripting/` subsystem
- [modules/runtime/README.md](../modules/runtime/README.md) — `game.dll` contract, `GameContext`
- [specs/scene-system.md](scene-system.md) — `.uzscene` script attachment syntax
