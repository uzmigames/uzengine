# UzEngine — Lua Scripting System

> **Audience**: Engine developers and AI agents generating game code
> **Scope**: LuaJIT VM lifecycle, script instance management, C++ binding patterns, error handling, hot-reload state persistence
> **Language**: C++23, Lua 5.1 (LuaJIT compatible)

---

## 1. Architecture Overview

The Lua scripting system is designed for **game logic authoring** with tight engine integration and **hot-reload safety**.

### 1.1 Module Boundary

```
┌─────────────────────────────────────────────────────┐
│ game.dll (Hot-Reloadable)                           │
│  - LuaVM (state, instance manager)                  │
│  - LuaScriptRunner (instance lifecycle)             │
│  - on_start, on_update, on_stop dispatch           │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│ uz_engine.lib (Stable, not reloaded)                │
│  - lua_binding.h/cpp (metatables, bindings)         │
│  - script_runner.h/cpp (generic runner template)    │
│  - lua_sandbox.h/cpp (security: strip forbidden)    │
│  - Note: NO LuaJIT linked here; only definitions    │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│ UzEngine.exe (Headless runtime)                     │
│  - Does not use Lua (headless mode)                 │
│  - Calls game.dll functions via C ABI              │
└─────────────────────────────────────────────────────┘
```

**Key Point**: LuaJIT is linked **only** in `game.dll`. When the DLL reloads, the Lua state is serialized, the old DLL is unloaded, the new DLL is loaded, and the state is deserialized.

### 1.2 Why This Design

| Benefit | Reason |
|---------|--------|
| **Isolated hot-reload** | Only Lua state is checkpointed; engine state (physics, rendering) unaffected |
| **Fast iteration** | Script changes apply immediately; no engine restart |
| **Graceful fallback** | If Lua script crashes, node script is disabled; other scripts unaffected |
| **Clear API contract** | C ABI boundary between engine and game logic |

---

## 2. LuaVM Lifecycle

The `LuaVM` class (in `game.dll`) manages the Lua state, script instances, and timer system.

### 2.1 Create

```cpp
// game.dll/lua_vm.h
class LuaVM {
public:
    static LuaVM* create(GameContext* ctx);
    static void destroy(LuaVM* vm);

    lua_State* state() const { return L; }
    GameContext* context() const { return ctx; }

private:
    lua_State* L;
    GameContext* ctx;
    hash_map<uint32_t, int> instance_refs;  // entity id → lua ref
    TimerQueue timers;
};

// Initialization sequence (game.dll/game.cpp on_start):
LuaVM* LuaVM::create(GameContext* ctx) {
    LuaVM* vm = new (ctx->level_arena) LuaVM();
    vm->ctx = ctx;

    // 1. Create new Lua state
    vm->L = luaJIT_newstate(lua_alloc, nullptr);
    if (!vm->L) return nullptr;

    // 2. Open standard libraries
    luaL_openlibs(vm->L);

    // 3. Apply sandbox (strip forbidden globals)
    lua_sandbox_apply(vm->L);

    // 4. Register all C++ bindings (metatables)
    lua_binding_register_all(vm->L);

    // 5. Inject global ctx table with engine API
    lua_binding_inject_ctx(vm->L, ctx);

    // 6. Load and instantiate all scene scripts
    for (auto entity : ctx->world.view<CScriptRef>()) {
        auto& script = ctx->world.get<CScriptRef>(entity);
        if (script.kind == SCRIPT_LUA) {
            load_script_instance(vm, entity, script);
        }
    }

    // 7. Initial timer drain (for startup behavior)
    vm->drain_timers(0.0f);

    return vm;
}
```

### 2.2 Script Instance Loading

Each CScriptRef entity gets a Lua script instance:

```cpp
static void load_script_instance(LuaVM* vm, entt::entity entity, CScriptRef& script) {
    lua_State* L = vm->state();
    StringView script_path = get_string(script.script_id);

    // 1. Load Lua file (luaL_loadfile internally)
    if (luaL_loadfile(L, script_path.data()) != LUA_OK) {
        log_error("[lua] Failed to load %s: %s", script_path.data(), lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
    }

    // 2. Create environment table for this instance (closure)
    lua_newtable(L);  // env = {}
    lua_pushvalue(L, -2);  // func on top
    lua_setfenv(L, -2);  // env becomes func's environment

    // 3. Call function once to define script methods (on_start, on_update, etc.)
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        log_error("[lua] Error initializing %s: %s", script_path.data(), lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
    }

    // 4. Store returned table (contains on_start, on_update, etc.) in registry
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    script.instance_ref = ref;
    script.state = (void*)vm;  // Back-pointer to LuaVM
}
```

### 2.3 Update Loop

```cpp
void update_frame(GameContext* ctx, float delta_time) {
    if (!ctx->lua) return;

    // ... physics step, input processing ...

    // Drain timers (fires schedule_after, schedule_every callbacks)
    ctx->lua->drain_timers(delta_time);

    // Script on_update dispatch
    for (auto entity : ctx->world.view<CScriptRef>()) {
        auto& script = ctx->world.get<CScriptRef>(entity);
        if (script.kind == SCRIPT_LUA && script.instance_ref != 0) {
            dispatch_lua_callback(ctx->lua, entity, script, "on_update", delta_time);
        }
    }

    // Other systems: rendering, audio, etc.
}

// Generic callback dispatch with error isolation
static void dispatch_lua_callback(LuaVM* vm, entt::entity entity,
                                  const CScriptRef& script,
                                  const char* method_name, float delta_time) {
    lua_State* L = vm->state();

    // 1. Push instance from registry
    lua_rawgeti(L, LUA_REGISTRYINDEX, script.instance_ref);
    if (lua_type(L, -1) != LUA_TTABLE) {
        lua_pop(L, 1);
        return;
    }

    // 2. Push method (e.g., "on_update")
    lua_getfield(L, -1, method_name);
    if (lua_type(L, -1) != LUA_TFUNCTION) {
        lua_pop(L, 2);
        return;
    }

    // 3. Push self = instance (upvalue for convenience)
    lua_pushvalue(L, -2);

    // 4. Push arguments (entity, delta_time, etc.)
    lua_pushuserdata(L, (void*)entity);
    lua_pushnumber(L, (lua_Number)delta_time);

    // 5. Call with error handling (lua_pcall, not lua_call)
    if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
        StringView path = get_string(script.script_id);
        log_error("[lua] %s::%s error: %s", path.data(), method_name, lua_tostring(L, -1));
        lua_pop(L, 1);

        // Disable script (prevent crash loop)
        const_cast<CScriptRef&>(script).instance_ref = 0;

        // Emit LuaErrorEvent for editor display
        ctx->event_bus.emit<LuaErrorEvent>({ entity, method_name, lua_tostring(L, -1) });
    }

    lua_pop(L, 1);  // Pop instance table
}
```

### 2.4 Destroy

```cpp
void LuaVM::destroy(LuaVM* vm) {
    if (!vm) return;

    lua_State* L = vm->state();

    // 1. Call on_stop for all scripts
    GameContext* ctx = vm->context();
    for (auto entity : ctx->world.view<CScriptRef>()) {
        auto& script = ctx->world.get<CScriptRef>(entity);
        if (script.kind == SCRIPT_LUA && script.instance_ref != 0) {
            dispatch_lua_callback(vm, entity, script, "on_stop", 0.0f);
        }
    }

    // 2. Release all Lua refs
    for (auto& [entity_id, ref] : vm->instance_refs) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
    vm->instance_refs.clear();

    // 3. Close Lua state
    lua_close(L);

    // 4. Deallocate VM object (from level arena)
    ctx->level_arena.dealloc(vm, sizeof(LuaVM));
}
```

---

## 3. Sandbox Security

Lua scripts are untrusted code. The sandbox strips dangerous APIs.

### 3.1 Stripped Globals

| Category | Status | Rationale |
|----------|--------|-----------|
| `io.*` | STRIPPED | File system access; player can read/write arbitrary files |
| `os.*` | STRIPPED | Process control, environment variables, system commands |
| `require` | STRIPPED | Dynamic module loading; can load arbitrary Lua code |
| `load`, `loadfile`, `dofile` | STRIPPED | Dynamic code evaluation |
| `package.*` | STRIPPED | Module system (incompatible with stripped require) |
| `debug.*` | STRIPPED* | Reflection, hook manipulation, CPU profiling; KEPT in `UZ_DEBUG=1` |
| `_G` | MODIFIED | Whitelist-only; forbidden keys deleted |
| `setfenv`, `getfenv` | STRIPPED | Environment manipulation (Lua 5.1 only) |
| `rawget`, `rawset`, `rawequal`, `rawlen` | STRIPPED | Bypass metatables; can access engine internals |

**Kept (SAFE)**:

```lua
-- Control flow
if, while, for, repeat-until, function, return, break

-- Data
local, nil, true, false, and, or, not

-- Library: math (no file I/O)
math.abs, math.sin, math.cos, math.sqrt, math.random, ...

-- Library: string (no pattern matching)
string.len, string.sub, string.upper, string.lower, string.byte, string.char, string.rep, string.reverse

-- Library: table (local structure only)
table.insert, table.remove, table.concat, table.sort

-- Iteration
pairs, ipairs, next

-- Type/value queries
type, tostring, tonumber, select, #, ==, <, >, ...

-- Error control
pcall, xpcall, error, assert

-- Engine bindings (injected as ctx.*)
ctx.scene, ctx.input, ctx.audio, ctx.saves, ctx.locale, ...
```

### 3.2 Sandbox Application

```cpp
// uz_engine.lib/lua_sandbox.h
void lua_sandbox_apply(lua_State* L) {
    // 1. Get global table
    lua_pushglobaltable(L);

    // 2. Delete forbidden keys
    const char* forbidden[] = {
        "io", "os", "require", "load", "loadfile", "dofile",
        "package", "debug", "setfenv", "getfenv", "rawget", "rawset",
        "rawequal", "rawlen", nullptr
    };

    for (const char** key = forbidden; *key; ++key) {
        lua_pushnil(L);
        lua_setfield(L, -2, *key);
    }

    // 3. Optional: restrict _G access
    lua_pushstring(L, "_G");
    lua_pushnil(L);
    lua_settable(L, -3);

    lua_pop(L, 1);  // Pop global table
}
```

---

## 4. C++ Binding System

### 4.1 Metatable Registration Pattern

Each C++ type gets a Lua metatable with methods and operators:

```cpp
// uz_engine.lib/lua_binding.cpp

// Vec3 metatable example
static int lua_vec3_new(lua_State* L) {
    float x = luaL_checknumber(L, 1);
    float y = luaL_checknumber(L, 2);
    float z = luaL_checknumber(L, 3);

    Vec3* v = (Vec3*)lua_newuserdata(L, sizeof(Vec3));
    *v = Vec3(x, y, z);

    luaL_setmetatable(L, "Vec3");
    return 1;
}

static int lua_vec3_add(lua_State* L) {
    Vec3* a = (Vec3*)luaL_checkudata(L, 1, "Vec3");
    Vec3* b = (Vec3*)luaL_checkudata(L, 2, "Vec3");

    Vec3* result = (Vec3*)lua_newuserdata(L, sizeof(Vec3));
    *result = *a + *b;

    luaL_setmetatable(L, "Vec3");
    return 1;
}

static int lua_vec3_mul(lua_State* L) {
    Vec3* v = (Vec3*)luaL_checkudata(L, 1, "Vec3");
    float s = luaL_checknumber(L, 2);

    Vec3* result = (Vec3*)lua_newuserdata(L, sizeof(Vec3));
    *result = *v * s;

    luaL_setmetatable(L, "Vec3");
    return 1;
}

static int lua_vec3_tostring(lua_State* L) {
    Vec3* v = (Vec3*)luaL_checkudata(L, 1, "Vec3");
    lua_pushfstring(L, "Vec3(%.2f, %.2f, %.2f)", v->x, v->y, v->z);
    return 1;
}

static void register_vec3(lua_State* L) {
    // Create metatable
    luaL_newmetatable(L, "Vec3");
    lua_pushcfunction(L, lua_vec3_add);
    lua_setfield(L, -2, "__add");
    lua_pushcfunction(L, lua_vec3_mul);
    lua_setfield(L, -2, "__mul");
    lua_pushcfunction(L, lua_vec3_tostring);
    lua_setfield(L, -2, "__tostring");

    // Methods
    lua_pushcfunction(L, lua_vec3_normalized);
    lua_setfield(L, -2, "normalized");
    lua_pushcfunction(L, lua_vec3_length);
    lua_setfield(L, -2, "length");
    lua_pushcfunction(L, lua_vec3_dot);
    lua_setfield(L, -2, "dot");
    lua_pushcfunction(L, lua_vec3_cross);
    lua_setfield(L, -2, "cross");

    // Index for properties (x, y, z via __index)
    lua_pushcfunction(L, lua_vec3_index);
    lua_setfield(L, -2, "__index");

    lua_pop(L, 1);  // Pop metatable

    // Register constructor
    lua_pushcfunction(L, lua_vec3_new);
    lua_setglobal(L, "Vec3");
}
```

### 4.2 Complete Binding Reference

| Type | Operators | Methods |
|------|-----------|---------|
| **Vec3** | `__add`, `__sub`, `__mul`, `__div`, `__unm`, `__eq` | `normalized()`, `length()`, `dot(v)`, `cross(v)` |
| **Quat** | `__mul` (quat*quat, quat*vec3), `__eq` | `inverse()`, `euler()`, `from_euler(v)` |
| **NodeRef** | `__eq` | `position()`, `rotation()`, `scale()`, `find_child(name)`, `get_tag(tag)`, `add_force(f)`, `play_audio(clip)`, `destroy()` |
| **Scene** | — | `find_node(name)`, `add_node(type, name, parent)`, `remove_node(node)`, `instantiate_prefab(path, parent)`, `get_root()` |
| **InputState** | — | `pressed(action)`, `held(action)`, `released(action)`, `axis(action)` |

### 4.3 Injection of ctx Table

```cpp
// uz_engine.lib/lua_binding.cpp
void lua_binding_inject_ctx(lua_State* L, GameContext* ctx) {
    // Create global ctx table
    lua_newtable(L);

    // ctx.scene
    lua_pushcclosure(L, lua_scene_find_node, 1);  // Upvalue: ctx
    lua_setfield(L, -2, "scene");

    // ctx.input
    lua_pushlightuserdata(L, (void*)&ctx->input);
    lua_pushcclosure(L, lua_input_pressed, 1);
    lua_setfield(L, -2, "input");

    // ctx.audio (subtable)
    lua_newtable(L);
    lua_pushcclosure(L, lua_audio_play_2d, 1);
    lua_setfield(L, -2, "play_2d");
    lua_pushcclosure(L, lua_audio_play_at, 1);
    lua_setfield(L, -2, "play_at");
    lua_setfield(L, -2, "audio");

    // ctx.saves (subtable)
    lua_newtable(L);
    lua_pushcclosure(L, lua_saves_save, 1);
    lua_setfield(L, -2, "save");
    lua_pushcclosure(L, lua_saves_load, 1);
    lua_setfield(L, -2, "load");
    lua_setfield(L, -2, "saves");

    // ctx.locale (subtable)
    lua_newtable(L);
    lua_pushcclosure(L, lua_locale_t, 1);
    lua_setfield(L, -2, "t");
    lua_setfield(L, -2, "locale");

    // ctx.log, ctx.delta_time, ctx.frame_index, etc.
    lua_pushcclosure(L, lua_log, 1);
    lua_setfield(L, -2, "log");

    // Set as global
    lua_setglobal(L, "ctx");
}
```

### 4.4 ctx Table Layout (Complete)

```lua
ctx = {
    -- Scene graph
    scene = {
        find_node(name),
        add_node(type, name, parent_node),
        remove_node(node),
        instantiate_prefab(path, parent),
        get_root(),
    },

    -- Input
    input = {
        pressed(action_name),
        held(action_name),
        released(action_name),
        axis(action_name),  -- float in [-1, 1]
    },

    -- Audio (XAudio2)
    audio = {
        play_2d(clip_id, volume, bus),
        play_at(clip_id, position, volume, bus),
        stop_all(bus),
        set_bus_volume(bus, volume),
    },

    -- Events
    events = {
        emit(event_type, data),
        subscribe(event_type, callback),
        unsubscribe(event_type, callback),
    },

    -- Save/Load
    saves = {
        save(slot, data_table),
        load(slot),
        autosave(),
        delete(slot),
        slot_exists(slot),
        get_metadata(slot),  -- {timestamp, playtime, scene_name}
    },

    -- Localization/I18n
    locale = {
        t(key),             -- Translate key (with fallback)
        tf(key, ...),       -- Translate with format string
        set(locale_code),   -- Switch language
    },

    -- In-game console
    console = {
        register(cmd_name, callback),
        execute(cmd_string),
    },

    -- Visual effects
    vfx = {
        spawn(effect_id, position, rotation),
        stop(effect_instance),
    },

    -- Navigation
    nav = {
        set_destination(agent, destination),
        stop(agent),
        has_path(agent),
        get_velocity(agent),
    },

    -- UI
    ui = {
        load(document_path),
        show(document_id),
        hide(document_id),
        unload(document_id),
        set_property(elem_id, prop, value),
        on_event(elem_id, event_type, callback),
        get(elem_id),
        is_modal(document_id),
    },

    -- Utility
    log(message),
    assert(condition, message),

    -- Time
    delta_time = 0.016,      -- Frame delta in seconds
    time_elapsed = 0.0,      -- Total elapsed since scene load
    frame_index = 0,         -- Current frame number
}
```

---

## 5. Timer System

Lua can schedule callbacks using the timer API:

```cpp
// game.dll/lua_vm.h
class TimerQueue {
    struct Timer {
        float delay;
        float elapsed = 0;
        bool repeating;
        int callback_ref;  // Lua function ref
        uint32_t entity_id;
    };

    void schedule_after(float delay, int callback_ref, uint32_t entity_id) {
        timers.push_back({ delay, 0, false, callback_ref, entity_id });
    }

    void schedule_every(float interval, int callback_ref, uint32_t entity_id) {
        timers.push_back({ interval, 0, true, callback_ref, entity_id });
    }

    void drain(float delta_time) {
        for (auto& timer : timers) {
            timer.elapsed += delta_time;
            if (timer.elapsed >= timer.delay) {
                call_lua_callback(timer.callback_ref, timer.entity_id);
                if (timer.repeating) {
                    timer.elapsed -= timer.delay;
                } else {
                    mark_for_removal(&timer);
                }
            }
        }
        remove_marked();
    }
};

// Lua API
static int lua_schedule_after(lua_State* L) {
    float delay = luaL_checknumber(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    LuaVM* vm = (LuaVM*)lua_touserdata(L, lua_upvalueindex(1));
    int callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    entt::entity self = *(entt::entity*)lua_touserdata(L, lua_upvalueindex(2));

    vm->timers.schedule_after(delay, callback_ref, (uint32_t)self);
    return 0;
}
```

---

## 6. Hot-Reload State Persistence

When `game.dll` reloads, the Lua state must be serialized and restored.

### 6.1 Serialization

```cpp
// game.dll/lua_vm.cpp
blob serialize_all_instances(LuaVM* vm) {
    lua_State* L = vm->state();
    SaveWriter writer;

    // Serialize each script instance's self table
    for (auto& [entity_id, ref] : vm->instance_refs) {
        // Get instance table
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        if (lua_type(L, -1) == LUA_TTABLE) {
            // Convert to msgpack (simplified: only handles basic types)
            serialize_table(writer, L, -1);
        }
        lua_pop(L, 1);
    }

    return writer.finalize();
}

// Simplified table serialization
void serialize_table(SaveWriter& w, lua_State* L, int idx) {
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {
        const char* key = lua_tostring(L, -2);
        if (lua_isnumber(L, -1)) {
            w.write_float(atof(key), (float)lua_tonumber(L, -1));
        } else if (lua_isstring(L, -1)) {
            w.write_string(key, lua_tostring(L, -1));
        } else if (lua_istable(L, -1)) {
            serialize_table(w, L, lua_gettop(L));  // Recurse
        }
        lua_pop(L, 1);
    }
}
```

### 6.2 Deserialization

```cpp
void deserialize_all_instances(LuaVM* vm, blob state_blob) {
    lua_State* L = vm->state();
    SaveReader reader(state_blob);

    for (auto& [entity_id, ref] : vm->instance_refs) {
        // Get instance table
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);

        // Overwrite with restored data
        while (reader.has_more()) {
            const char* key = reader.read_key();
            if (reader.peek_type() == TYPE_FLOAT) {
                lua_pushnumber(L, reader.read_float());
            } else if (reader.peek_type() == TYPE_STRING) {
                lua_pushstring(L, reader.read_string());
            } else if (reader.peek_type() == TYPE_TABLE) {
                // Nested table — recurse
            }
            lua_setfield(L, -2, key);
        }

        lua_pop(L, 1);
    }
}
```

---

## 7. Error Handling

All script callbacks use `lua_pcall` for isolation:

```cpp
// If any script errors:
// 1. Log the error message
// 2. Emit LuaErrorEvent for editor UI (red in Scene Tree)
// 3. Disable the script (set instance_ref = 0)
// 4. Continue executing other scripts
// 5. Frame proceeds normally (no engine crash)

// Example in engine:
dispatch_lua_callback(vm, entity, script, "on_update", dt);
// If error, script is silently disabled above
// Other scripts still run
```

---

## 8. Related Documentation

- [Script System Specification](../specs/lua-scripting.md) — Full Lua API, script loading, error handling
- [ECS and Scene System](ecs-and-scene.md) — CScriptRef component, entity lifecycle, script instantiation
- [Audio System](audio-system.md) — ctx.audio bindings, XAudio2 integration
- [Lifecycle and Concurrency](lifecycle-and-concurrency.md) — Game loop, thread safety, frame synchronization

---

## 9. Key Takeaways

1. **LuaJIT lives in game.dll only** — Engine state survives hot-reload
2. **Scripts are sandboxed** — No file I/O, process control, or dynamic code loading
3. **Errors are isolated** — One script crash doesn't affect others or engine
4. **State persists across reload** — Serialize self tables before unload, restore after load
5. **ctx table is the API** — All engine bindings available via ctx.*
6. **Timers drain per-frame** — schedule_after and schedule_every work predictably
7. **Metatables enable type safety** — Vec3, Quat, NodeRef are first-class Lua types
