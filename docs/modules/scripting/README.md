# Module: Scripting (LuaJIT)

**Location:** `src/engine/scripting/` (binding helpers) + `src/game/` (VM)
**Output:** Binding helpers → `uz_engine.lib`; VM → `game.dll`
**Language:** C++23 + Lua 5.1 (LuaJIT)
**Role:** Per-node Lua scripting with hot-reload, sandboxing, and C++ binding metatables.

---

## Responsibility

The Scripting module provides per-node Lua scripting, where each CScriptRef component can execute a `.lua` file. The module handles:

- **VM lifecycle**: Create/destroy LuaJIT state in game.dll
- **C++ bindings**: Metatables for Vec3, Quat, NodeRef, Scene, Input, Audio, etc.
- **Sandboxing**: Disable dangerous functions (io.*, os.*, require, debug.*)
- **Hot-reload**: FileWatcher → LuaScriptReloader → on_stop → reload → on_start
- **Error isolation**: lua_pcall failures disable the node, log the error, engine continues
- **State persistence**: Serialize all Lua tables for game.dll hot-reload

---

## Critical Split: Binding Helpers vs VM

This is a **crucial architectural boundary**:

```
uz_engine.lib (no LuaJIT linked)
├─ lua_binding.h/cpp        ← Binding metatables ONLY
├─ script_runner.h/cpp      ← Dispatch helpers (on_start, on_update, on_stop)
└─ lua_sandbox.h/cpp        ← Sandbox config (stripped functions)

game.dll (LuaJIT linked here ONLY)
├─ lua_vm.h/cpp             ← lua_State creation, table serialization
└─ lua_script_reloader.h/cpp← FileWatcher subscriber, hot-reload coordinator
```

**Why this split?**

1. **uz_engine.lib never links LuaJIT** — smaller, faster compile, no LuaJIT dependency at engine level
2. **game.dll owns the VM** — scripting state lives with game logic; resets on reload
3. **Bindings are header-only helpers** — can be used by both uz_engine and game.dll without circular deps

---

## Source Structure

### uz_engine.lib (Binding Helpers)

```cpp
// src/engine/scripting/lua_binding.h
// Forward declaration (no LuaJIT include)
struct lua_State;

// Metatables registered by game.dll
struct LuaBindings {
    static void register_vec3(lua_State* L);
    static void register_quat(lua_State* L);
    static void register_node_ref(lua_State* L);
    static void register_scene(lua_State* L);
    static void register_input_state(lua_State* L);
};

// src/engine/scripting/script_runner.h
struct ScriptRunner {
    // Called by game.dll to execute Lua callbacks
    static void call_on_start(lua_State* L, int fn_ref, Handle<CNode> node);
    static void call_on_update(lua_State* L, int fn_ref, float dt, Handle<CNode> node);
    static void call_on_stop(lua_State* L, int fn_ref, Handle<CNode> node);
};

// src/engine/scripting/lua_sandbox.h
struct LuaSandbox {
    // Remove dangerous functions from global table
    static void install_sandbox(lua_State* L);
    // Strips: io.*, os.*, require, load, debug.*, collectgarbage
};
```

### game.dll (VM Ownership)

```cpp
// src/game/lua_vm.h
struct LuaVM {
    LuaVM();
    ~LuaVM();

    void load_script(Handle<CNode> node, const char* path);
    void drain_timers(float dt);
    void serialize_all_instances(SaveWriter* writer);
    void deserialize_all_instances(SaveReader* reader);

    lua_State* state() { return m_state; }

private:
    lua_State* m_state;
    std::unordered_map<uint32_t, int> m_node_fn_refs;  // node_id → Lua func ref
};

// src/game/lua_script_reloader.h
struct LuaScriptReloader : FileWatcher::Subscriber {
    void on_file_changed(const Path& path) override;
    // Triggers: on_stop → reload → on_start
};
```

---

## Metatable Overview

| Type | Methods | Example |
|------|---------|---------|
| **Vec3** | `+`, `-`, `*`, `/`, `dot`, `cross`, `length`, `normalize`, `.x/.y/.z` | `v1 + v2`, `v:normalize()` |
| **Quat** | `*` (quat mul), `slerp`, `get_forward`, `get_up`, `get_right` | `q1 * q2`, `q:get_forward()` |
| **NodeRef** | `get_child`, `add_child`, `remove`, `get_component`, `set_active`, `.name`, `.transform` | `node:get_component("RigidBody")` |
| **Scene** | `find_by_tag`, `find_by_name`, `raycasts`, `overlaps` | `scene:find_by_tag("enemy")` |
| **InputState** | `is_key_pressed`, `is_key_held`, `mouse_pos`, `scroll_delta` | `input:is_key_pressed("Space")` |
| **Transform** | `get_position`, `set_position`, `get_rotation`, `set_rotation`, `translate`, `rotate` | `transform:translate(Vec3(1, 0, 0))` |

---

## Script Module Pattern

Typical script structure (per CScriptRef node):

```lua
-- player.lua
local M = {}

function M.on_start(self, ctx)
    -- Called once when node spawns
    self.health = 100
    self.speed = 5.0
    print("Player initialized")
end

function M.on_update(self, ctx, dt)
    -- Called every frame
    local input = ctx.input

    if input:is_key_held("W") then
        self.node:get_component("Transform"):translate(Vec3(0, 0, self.speed * dt))
    end

    -- Access physics
    local rb = self.node:get_component("RigidBody")
    if rb then
        rb:apply_force(Vec3(0, -9.81, 0))
    end
end

function M.on_stop(self, ctx)
    -- Called when node is removed
    print("Player destroyed")
end

return M
```

When loaded via `node:attach_script("player.lua")`:
1. luaL_loadfile() → parse script
2. lua_pcall() → execute, returns module table
3. Store module.on_start as Lua ref
4. Each frame: lua_pcall(on_update, ...)
5. On stop: lua_pcall(on_stop, ...)

---

## Error Isolation

Lua errors are isolated — script failure doesn't crash the engine:

```cpp
void call_on_update(Handle<CNode> node, const CScriptRef& script, float dt) {
    lua_State* L = m_lua_vm.state();

    // lua_pcall catches exceptions and errors
    int err = lua_pcall(L, 3, 0, 0);  // args: self, ctx, dt

    if (err != 0) {
        // Lua error — extract message from stack
        const char* msg = lua_tostring(L, -1);
        log_error("Script error in {}: {}", script.path, msg);

        // Disable the node (visible in Scene Tree)
        node->set_active(false);

        // Engine continues normally
        lua_pop(L, 1);
    }
}
```

Result: One broken script doesn't freeze the game. Developer sees error in Console.

---

## Hot-Reload Flow

FileWatcher detects `.lua` file changes and triggers reload:

```
1. Editor saves player.lua
2. FileWatcher emits on_file_changed("player.lua")
3. LuaScriptReloader receives event
4. For each node using player.lua:
   a. lua_pcall(on_stop) — cleanup
   b. luaL_loadfile(player.lua) — parse new version
   c. lua_pcall(on_start) — reinitialize with NEW code
5. Frame continues — script runs with new behavior

Duration: ~60ms (file I/O + Lua parse + 2 pcall frames)
```

State is **NOT** preserved across reload (fresh on_start execution). Developers save state explicitly:

```lua
function M.on_stop(self, ctx)
    -- Save state before reload
    ctx.saves:set_key("player_health", self.health)
end

function M.on_start(self, ctx)
    -- Restore state after reload
    self.health = ctx.saves:get_key("player_health", 100)
end
```

---

## State Persistence for game.dll Reload

When game.dll hot-reloads (0.3 seconds), Lua instance tables must be serialized:

```cpp
// Before unload game.dll:
void on_save_state(GameContext* ctx, SaveWriter* writer) {
    lua_vm->serialize_all_instances(writer);
    // Writes all "self" tables for all active nodes to msgpack blob
}

// In game.dll::LuaVM::serialize_all_instances:
void serialize_all_instances(SaveWriter* writer) {
    for (auto& [node_id, fn_ref] : m_node_fn_refs) {
        // Get the "self" table
        lua_rawgeti(m_state, LUA_REGISTRYINDEX, fn_ref);  // Push func
        lua_getupvalue(m_state, -1, 1);  // Get "self" from closure

        // Write to msgpack
        serialize_table_to_msgpack(m_state, -1, writer);
        lua_pop(m_state, 2);
    }
}

// After reload:
void on_load_state(GameContext* ctx, SaveReader* reader) {
    lua_vm->deserialize_all_instances(reader);
    // Re-create all "self" tables from msgpack blob
}
```

This preserves **all runtime Lua state** across game.dll reload, invisible to the developer.

---

## Timer Integration

Lua timers are drained BEFORE node on_update each frame:

```cpp
void game_update(float dt) {
    // 1. Drain timers (fire schedule_after/schedule_every callbacks)
    lua_vm->drain_timers(dt);

    // 2. Script on_update (now can query timer state)
    for (auto node : active_nodes) {
        auto* script = node->get_component<CScriptRef>();
        if (script) script->on_update(dt);
    }
}
```

Lua API (in ctx.timers):
```lua
ctx.timers:schedule_after(1.0, function() print("Hi") end)
ctx.timers:schedule_every(0.5, function() print("Tick") end)
handle = ctx.timers:schedule_next_frame(function() ... end)
ctx.timers:cancel(handle)
```

---

## Phase Roadmap

**Phase 1**:
- Core scripting (create/destroy VM)
- C++ bindings for Vec3, Quat, NodeRef, Scene
- Sandbox (strip dangerous functions)
- on_start / on_update / on_stop dispatch
- Hot-reload coordinator
- Error isolation via lua_pcall
- Diagnostics: lua.get_errors IPC

**Phase 1.5**:
- Timer system (schedule_after, schedule_every, cancel_timer)
- Timer drain before on_update

**Phase 3**:
- EventBus Lua layer (emit/subscribe via ctx.events)
- Physics queries in Lua
- Audio playback in Lua
- Input state in Lua

**Phase 4**:
- Debugger integration (breakpoints, watch, step)
- Profiler integration (per-function CPU time)
- REPL console for live coding

---

## Related Docs

- [Game Module](../game/README.md) — on_update, timer drain, state persistence
- [Lua Scripting Spec](../../specs/lua-scripting.md) — Full design, Context API, all bindings
- [Guides: Lua Scripting Guide](../../guides/lua-scripting-guide.md) — Practical patterns, best practices
- [Event Bus Spec](../../specs/event-bus.md) — EventBus integration with Lua
- [Timer System Spec](../../specs/timer-system.md) — Timer API design
