# Script Execution Safety Spec

> **Status**: Planned (Phase 1)
> **Priority**: P0 — Safety Blocker
> **Rationale**: `while true do end` in any Lua script freezes render.exe entirely — the game logic thread stalls, the IPC server becomes unresponsive, and the editor appears to crash. This is the #1 complaint in any scripting system. Must be fixed before Lua scripting ships to users.

---

## Overview

**Script Execution Safety** provides:
1. **CPU instruction budget** — interrupt Lua after N million instructions
2. **Memory cap** — prevent Lua from allocating beyond a per-VM limit
3. **Sandbox escape detection** — detect attempts to access forbidden globals
4. **Error recovery** — recover gracefully without restarting the engine
5. **Watchdog thread** — terminate scripts that survive the instruction hook

---

## Problem: Lua Infinite Loops

LuaJIT executes compiled bytecode on the game logic thread. There is no preemptive OS-level interruption. A script can spin forever:

```lua
-- Freezes engine forever — unacceptable
function on_update(dt)
  while true do end
end
```

The instruction hook is the standard Lua solution, but LuaJIT compiles hot loops to native code that **bypasses** the hook. Both mechanisms are required.

---

## Solution 1: Lua Instruction Hook (interpreted paths)

```cpp
// Install during VM creation (game.dll LuaVM::init)
lua_sethook(L, lua_timeout_hook, LUA_MASKCOUNT, INSTRUCTION_BUDGET);

static void lua_timeout_hook(lua_State* L, lua_Debug*) {
    if (script_execution_exceeded_budget(L)) {
        luaL_error(L, "Script execution timeout: exceeded %d instructions", INSTRUCTION_BUDGET);
    }
}
```

`INSTRUCTION_BUDGET` default: `5,000,000` instructions (~50ms at 100 MIPS).

Per-script override in `.uzscene`:
```toml
[node.player_controller]
script = "scripts/player.lua"
script_instruction_budget = 1000000  # Override for performance-critical scripts
```

---

## Solution 2: JIT Compiled Loop Detection (LuaJIT)

LuaJIT's `jit.on()` means hot loops run as native code, bypassing the hook. Counter:

```cpp
// Option A: Disable JIT for untrusted scripts
lua_getglobal(L, "jit");
lua_getfield(L, -1, "off");
lua_call(L, 0, 0);  // jit.off() — safe, ~3x slower

// Option B: Use LuaJIT's LUAJIT_DISABLE_JIT compile flag (engine-wide)
// Option C: Signal-based watchdog (recommended for production)
```

**Recommendation**: Disable JIT on developer scripts, keep JIT for shipped game.dll.

---

## Solution 3: Watchdog Thread

A separate watchdog thread monitors script execution time:

```cpp
class ScriptWatchdog {
    std::atomic<bool>     script_running_{false};
    std::atomic<uint64_t> script_start_ns_{0};
    std::thread           watchdog_thread_;
    static constexpr uint64_t TIMEOUT_NS = 100'000'000; // 100ms wall-clock

public:
    void begin_execution(lua_State* L) {
        L_ = L;
        script_start_ns_.store(now_ns(), std::memory_order_release);
        script_running_.store(true, std::memory_order_release);
    }

    void end_execution() {
        script_running_.store(false, std::memory_order_release);
    }

private:
    void watchdog_loop() {
        while (!shutdown_) {
            if (script_running_.load() &&
                now_ns() - script_start_ns_.load() > TIMEOUT_NS)
            {
                // Force Lua to error on next instruction check
                lua_sethook(L_, force_error_hook, LUA_MASKLINE | LUA_MASKCOUNT, 1);
                script_running_.store(false, std::memory_order_release);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    static void force_error_hook(lua_State* L, lua_Debug*) {
        luaL_error(L, "Script watchdog: execution exceeded 100ms wall-clock");
    }
};
```

---

## Solution 4: Memory Allocator Cap

```cpp
// Custom Lua allocator with per-VM memory limit
struct LuaAllocator {
    size_t used    = 0;
    size_t max_mem = 32 * 1024 * 1024; // 32 MB default per script VM

    static void* alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
        auto* a = static_cast<LuaAllocator*>(ud);
        a->used -= osize;
        if (nsize == 0) { free(ptr); return nullptr; }
        if (a->used + nsize > a->max_mem) {
            // Allocation would exceed cap — return nullptr (triggers Lua OOM error)
            a->used += osize;
            return nullptr;
        }
        a->used += nsize;
        return realloc(ptr, nsize);
    }
};

// Install at VM creation:
lua_State* L = lua_newstate(LuaAllocator::alloc, &allocator);
```

---

## Error Recovery

When a script times out or crashes, the engine must recover cleanly:

```cpp
// In ScriptRunner::execute() — wraps all Lua calls
ScriptError ScriptRunner::execute_protected(lua_State* L, const char* fn_name) {
    watchdog_.begin_execution(L);

    int status = lua_pcall(L, 0, 0, err_handler_ref_);

    watchdog_.end_execution();

    if (status != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        lua_pop(L, 1);

        // Log to VSCode diagnostics panel
        ipc_send_error({ .file = current_script_, .message = err });

        // Mark script as errored — skip on_update until reloaded
        script_state_ = ScriptState::Errored;

        return ScriptError{ .message = err, .is_timeout = is_timeout_error(err) };
    }
    return ScriptError::None;
}
```

---

## VSCode Diagnostics Integration

When a script errors (including timeout), send to VSCode:

```json
{
  "type": "lua.script_error",
  "file": "scripts/player.lua",
  "line": 42,
  "message": "Script execution timeout: exceeded 5000000 instructions",
  "is_timeout": true,
  "stack": "scripts/player.lua:42: in function 'on_update'\n..."
}
```

VSCode extension displays a red error in the Problems panel and decorates the offending line.

---

## Sandbox Escape Detection

```cpp
// Called from __newindex metamethod on _G during initialization
static int sandbox_newindex(lua_State* L) {
    const char* key = lua_tostring(L, 2);

    // Log attempted global modification
    ipc_send_warning({
        .type    = "lua.sandbox_escape",
        .script  = current_script(L),
        .message = std::format("Attempt to write global '{}' — blocked", key)
    });

    return 0;  // Silently block (don't error — may be in pcall)
}
```

---

## Configuration (.uzproj)

```toml
[scripting]
instruction_budget   = 5000000   # Per on_update call
watchdog_timeout_ms  = 100       # Wall-clock timeout (watchdog thread)
memory_limit_mb      = 32        # Per-VM memory cap
jit_enabled          = false     # Disable JIT for editor scripts (enable for shipped games)
sandbox_log_escapes  = true      # Log sandbox violations to diagnostics
```

---

## Unit Tests

```cpp
TEST(ScriptSafety, InfiniteLoopIsTerminated) {
    LuaVM vm;
    vm.load_script("function on_update() while true do end end");
    auto result = vm.call("on_update");
    EXPECT_TRUE(result.is_error());
    EXPECT_THAT(result.message, HasSubstr("timeout"));
}

TEST(ScriptSafety, MemoryCapPreventsOOM) {
    LuaVM vm;
    vm.set_memory_limit(1 * 1024 * 1024); // 1 MB
    vm.load_script("function blow_memory() local t = {} for i=1,1e9 do t[i]=i end end");
    auto result = vm.call("blow_memory");
    EXPECT_TRUE(result.is_error());
}

TEST(ScriptSafety, CleanRecoveryAfterError) {
    LuaVM vm;
    vm.load_script("function on_update() error('intentional') end");
    vm.call("on_update");  // Error — but VM survives
    vm.load_script("function on_update() end");  // Reload
    EXPECT_FALSE(vm.call("on_update").is_error());  // Works again
}
```

---

## Implementation Plan

| Week | Deliverable |
|------|-------------|
| 1 | Instruction hook + memory allocator cap |
| 2 | Watchdog thread + force-error mechanism |
| 3 | VSCode diagnostics integration (IPC error events) |
| 4 | Sandbox escape detection + unit tests |

---

## Dependencies

- LuaJIT integration (Phase 1 Lua scripting)
- IPC protocol (diagnostics channel)
- VSCode extension (Problems panel integration)

## Related Specs

- [Lua Scripting](lua-scripting.md)
- [Console Commands](console-commands.md)
- [Crash Reporting](crash-reporting.md)
