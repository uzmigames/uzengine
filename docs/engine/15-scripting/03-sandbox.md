# Scripting — Security Sandbox

> **Status**: Planned (Phase 3)
> **Source**: `engine/scripting/lua_sandbox.h`, `lua_sandbox.cpp`
> **Depends on**: LuaVM (§15.2), subsystem registry (§9.4)
> **References**: Roblox Lua sandboxing, Garry's Mod Lua restrictions

## Purpose

The sandbox restricts what Lua scripts can do. Dangerous globals (`os.execute`, `io.open`, `debug.*`) are stripped, and execution time/memory limits are enforced. This prevents malicious or buggy scripts from crashing the engine or accessing the filesystem.

## Threat Model

```
Untrusted Lua Script
        │
        ├─ Infinite loop → Stall engine (timeout)
        ├─ Huge allocation → Exhaust memory (limit)
        ├─ os.execute() → Execute arbitrary commands (BLOCKED)
        ├─ io.open() → Read/write files (BLOCKED)
        ├─ load() → Dynamic code generation (BLOCKED)
        ├─ debug.setlocal() → Inspect other scripts (BLOCKED)
        └─ Correct usage → OK (passes through)
```

## Sandbox Architecture

```
Lua State
    │
    ├─ Global table (stripped)
    │   ├─ os (REMOVED)
    │   ├─ io (REMOVED)
    │   ├─ load (REMOVED)
    │   ├─ loadfile (REMOVED)
    │   ├─ dofile (REMOVED)
    │   ├─ debug (REMOVED, except debug.traceback)
    │   ├─ math (SAFE, kept)
    │   ├─ string (SAFE, kept)
    │   ├─ table (SAFE, kept)
    │   ├─ coroutine (SAFE, kept)
    │   └─ print (SAFE, redirected to engine logging)
    │
    ├─ Execution limits
    │   ├─ Instruction count (abort if exceeded)
    │   ├─ Call stack depth (prevent stack overflow)
    │   └─ Time limit (1 second per script execution)
    │
    └─ Metatable locks
        └─ Can't modify __index, __newindex on core types
```

## Sandboxing Process

When a World is created, the Lua VM is sandboxed:

```cpp
void SandboxLuaVM(LuaVM* vm) {
    lua_State* L = vm->GetState();

    // 1. Remove dangerous libraries
    RemoveDangerousGlobals(L);

    // 2. Remove dynamic code evaluation
    DisableCodeGeneration(L);

    // 3. Remove debug introspection
    RestrictDebugLibrary(L);

    // 4. Install execution limits
    InstallInstructionCounter(L, 1000000);  // Max ~1M ops per call
    InstallTimeoutCounter(L, 1.0f);         // Max 1 second per call

    // 5. Prevent metatable modifications
    LockMetatables(L);

    UZ_LOG(Scripting, Info, "Lua VM sandboxed");
}
```

## Step 1: Remove Dangerous Globals

```cpp
void RemoveDangerousGlobals(lua_State* L) {
    // Remove os library
    lua_pushnil(L);
    lua_setglobal(L, "os");

    // Remove io library
    lua_pushnil(L);
    lua_setglobal(L, "io");

    // Remove loadfile/dofile
    lua_pushnil(L);
    lua_setglobal(L, "loadfile");

    lua_pushnil(L);
    lua_setglobal(L, "dofile");

    // Keep safe libraries
    // math, string, table, coroutine remain
}
```

## Step 2: Restrict Code Generation

```cpp
void DisableCodeGeneration(lua_State* L) {
    // load() and loadstring() allow executing arbitrary Lua code
    lua_pushnil(L);
    lua_setglobal(L, "load");

    lua_pushnil(L);
    lua_setglobal(L, "loadstring");
}
```

## Step 3: Restrict Debug Library

```cpp
void RestrictDebugLibrary(lua_State* L) {
    // Get debug library
    lua_getglobal(L, "debug");

    if (lua_istable(L, -1)) {
        // Remove dangerous debug functions
        lua_pushnil(L);
        lua_setfield(L, -2, "setlocal");    // Can't modify script variables

        lua_pushnil(L);
        lua_setfield(L, -2, "setfenv");     // Can't change environment

        lua_pushnil(L);
        lua_setfield(L, -2, "sethook");     // Can't hook into execution

        lua_pushnil(L);
        lua_setfield(L, -2, "getupvalue");  // Can't read closure variables

        // Keep debug.traceback() for error reporting
        // Keep getinfo for stack traces
    }

    lua_pop(L, 1);  // Pop debug table
}
```

## Step 4: Instruction Counter (Execution Limit)

Lua has a `lua_sethook` mechanism to pause execution at regular intervals:

```cpp
void InstallInstructionCounter(lua_State* L, int max_instructions) {
    // Store limit in registry
    lua_pushlightuserdata(L, (void*)&MAX_INSTRUCTIONS_KEY);
    lua_pushinteger(L, max_instructions);
    lua_settable(L, LUA_REGISTRYINDEX);

    // Install hook to count instructions
    lua_sethook(L, InstructionHook, LUA_MASKCOUNT, 1000);
    // LUA_MASKCOUNT: Called every N instructions (1000 in this case)
}

static void InstructionHook(lua_State* L, lua_Debug* ar) {
    lua_pushlightuserdata(L, (void*)&INSTRUCTION_COUNT_KEY);
    lua_gettable(L, LUA_REGISTRYINDEX);
    int current_count = lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_pushlightuserdata(L, (void*)&MAX_INSTRUCTIONS_KEY);
    lua_gettable(L, LUA_REGISTRYINDEX);
    int max_count = lua_tointeger(L, -1);
    lua_pop(L, 1);

    if (current_count >= max_count) {
        // Exceeded instruction limit, abort
        luaL_error(L, "Lua script exceeded instruction limit");
    }

    // Increment counter
    current_count += 1000;
    lua_pushlightuserdata(L, (void*)&INSTRUCTION_COUNT_KEY);
    lua_pushinteger(L, current_count);
    lua_settable(L, LUA_REGISTRYINDEX);
}
```

## Step 5: Timeout Counter (Time Limit)

```cpp
void InstallTimeoutCounter(lua_State* L, float timeout_seconds) {
    lua_pushlightuserdata(L, (void*)&TIMEOUT_SECONDS_KEY);
    lua_pushnumber(L, timeout_seconds);
    lua_settable(L, LUA_REGISTRYINDEX);

    lua_pushlightuserdata(L, (void*)&START_TIME_KEY);
    lua_pushnumber(L, GetCurrentTime());
    lua_settable(L, LUA_REGISTRYINDEX);

    lua_sethook(L, TimeoutHook, LUA_MASKCOUNT, 10000);
}

static void TimeoutHook(lua_State* L, lua_Debug* ar) {
    double start_time = GetRegistryDouble(L, &START_TIME_KEY);
    double timeout = GetRegistryDouble(L, &TIMEOUT_SECONDS_KEY);
    double elapsed = GetCurrentTime() - start_time;

    if (elapsed > timeout) {
        luaL_error(L, "Lua script timeout (%.2f s)", elapsed);
    }
}
```

## Step 6: Lock Metatables

Prevent scripts from modifying core types' metatables:

```cpp
void LockMetatables(lua_State* L) {
    // Get Vec3 metatable
    luaL_getmetatable(L, "Vec3");
    if (!lua_isnil(L, -1)) {
        // Make it read-only by replacing __newindex
        lua_pushcclosure(L, MetatableReadOnlyIndex, 0);
        lua_setfield(L, -2, "__newindex");
    }
    lua_pop(L, 1);

    // Repeat for Entity, World, etc.
}

static int MetatableReadOnlyIndex(lua_State* L) {
    luaL_error(L, "Cannot modify core type metatable");
    return 0;
}
```

## Safe Redirects

Dangerous functions are redirected to safe alternatives:

```cpp
void RedirectUnsafeGlobals(lua_State* L) {
    // print → UZ_LOG
    lua_pushcclosure(L, SafePrint, 0);
    lua_setglobal(L, "print");

    // print is now:
    // function print(...) uz_log(table.concat({...})) end
}

static int SafePrint(lua_State* L) {
    // Get number of arguments
    int n = lua_gettop(L);

    std::string output;
    for (int i = 1; i <= n; ++i) {
        if (i > 1) output += "\t";
        output += lua_tostring(L, i);
    }

    UZ_LOG(Scripting, Info, "{}", output);
    return 0;
}
```

## Sandbox Configuration

```cpp
struct SandboxConfig {
    /// Maximum instructions before timeout
    int max_instructions = 1000000;

    /// Maximum time in seconds
    float timeout_seconds = 1.0f;

    /// Maximum call stack depth (prevent stack overflow)
    int max_call_depth = 100;

    /// Maximum memory in bytes (enforced by allocator)
    size_t max_heap_bytes = 64 * 1024 * 1024;

    /// Allow file I/O (dangerous, disable for untrusted scripts)
    bool allow_file_io = false;

    /// Allow os.* functions
    bool allow_os = false;

    /// Allow debug.* functions (except unsafe ones)
    bool allow_debug_traceback = true;
};

void ApplySandboxConfig(LuaVM* vm, const SandboxConfig& cfg) {
    lua_State* L = vm->GetState();

    InstallInstructionCounter(L, cfg.max_instructions);
    InstallTimeoutCounter(L, cfg.timeout_seconds);

    if (!cfg.allow_file_io) {
        RemoveDangerousGlobals(L);
    }
    if (!cfg.allow_os) {
        lua_pushnil(L);
        lua_setglobal(L, "os");
    }
    if (!cfg.allow_debug_traceback) {
        RestrictDebugLibrary(L);
    }

    UZ_LOG(Scripting, Info, "Sandbox applied: timeout={} s, max_instructions={}",
           cfg.timeout_seconds, cfg.max_instructions);
}
```

## Testing Sandbox Escapes

```cpp
void TestSandbox() {
    LuaVM* vm = LuaVM::Create({});

    // Try to escape: should fail
    const char* escape_attempt = R"(
        os.execute("echo hacked")  -- Should fail: os is nil
    )";

    std::string error;
    if (!vm->ExecuteScript(escape_attempt)) {
        UZ_LOG(Scripting, Info, "Sandbox prevented escape: os.execute");
    }

    // Try infinite loop: should timeout
    const char* infinite_loop = R"(
        while true do end  -- Should timeout after 1 second
    )";

    if (!vm->ExecuteScript(infinite_loop)) {
        UZ_LOG(Scripting, Info, "Sandbox prevented infinite loop");
    }
}
```

## Safe Script Patterns

Scripts should follow these patterns:

```lua
-- SAFE: Pure Lua math
local function fibonacci(n)
    if n <= 1 then return n end
    return fibonacci(n - 1) + fibonacci(n - 2)
end

-- SAFE: Use engine APIs
function on_update(ctx, dt)
    local entity = ctx.entity
    local pos = entity:get_position()
    entity:set_position(pos + Vec3(1, 0, 0) * dt)
end

-- UNSAFE: Infinite loop (will timeout)
function bad_loop()
    while true do
        -- Stuck forever
    end
end

-- UNSAFE: Huge allocation (will fail)
function bad_alloc()
    local huge_table = {}
    for i = 1, 1000000 do
        huge_table[i] = string.rep("x", 1000)
    end
end
```

## Engine Comparison

| Aspect | Roblox | Garry's Mod | UzEngine |
|--------|--------|------------|----------|
| Sandbox | Yes, per-script limits | Yes (ulx modules) | **Per-VM limits + instruction counting** |
| Forbidden globals | `_G.os`, `_G.io` (removed) | Custom whitelist | **Removed: os, io, load, loadfile, dofile, debug.*** |
| Timeout | No built-in | Manual script limits | **1s per script + instruction counter** |
| Memory limit | Game memory pool | Manual | **Configurable, default 64MB** |

## Rules

1. **ALWAYS sandbox Lua VMs** — untrusted scripts can crash engine
2. **NEVER allow `load()` or `loadstring()`** — enables arbitrary code execution
3. **ALWAYS set instruction/timeout limits** — prevent infinite loops and stalls
4. **NEVER expose `debug.*` fully** — allows introspection of other scripts
5. **ALWAYS log sandbox violations** — helps identify malicious/buggy scripts
