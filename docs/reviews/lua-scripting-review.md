# Lua Scripting System — AAA Gap Analysis

**Author**: Lua Scripting Engineer
**Date**: 2026-03-02
**Scope**: Analysis of `lua-scripting.md`, `timer-system.md`, `event-bus.md`, `console-commands.md`, `ai-native-features.md`, `engine-vision.md`, `ROADMAP.md`
**Verdict**: The current spec is a solid foundation for indie/mid-tier. For AAA-quality scripting, **17 critical gaps** must be addressed.

---

## Executive Summary

The UzEngine Lua scripting specification covers the fundamentals well: sandboxed VM, per-node instance model, hot-reload, basic error handling, and a reasonable set of engine bindings. However, when measured against the scripting layers of production engines (Unreal Blueprints + C++, Unity C# + Burst, Godot GDScript, Defold Lua, Roblox Luau, CryEngine Schematyc + Lua), several categories of functionality are either absent or underspecified.

This review identifies gaps that will become **blocking issues** as UzEngine scales beyond simple prototyping into production gameplay programming. Each gap is analyzed with reference to real-world engines, technical depth, impact assessment, complexity estimate, and dependency chain.

---

## Gap 1: Coroutines / Async Primitives for Gameplay Sequencing

### Why Critical

Every production game engine provides a way to write sequential gameplay code that spans multiple frames. Unity has `StartCoroutine` + `yield return`. Unreal has Latent Actions and Gameplay Abilities with async tasks. Godot has `await` on signals. Defold uses Lua coroutines natively. Without this, **every multi-frame behavior** (camera lerps, dialogue sequences, patrol wait points, attack combos, cutscenes) must be hand-rolled as a state machine in `on_update`, which is error-prone, verbose, and hostile to AI code generation.

### Current State in Spec

The `timer-system.md` spec provides `schedule_after` and `schedule_every`, which handle simple delays. But there is no way to express:

```lua
-- THIS IS NOT POSSIBLE IN THE CURRENT SPEC
function M:attack_sequence(ctx)
    self:play_anim("wind_up")
    wait(0.5)                          -- yield for 0.5 seconds
    self:play_anim("strike")
    local hit = ctx.world:raycast(...)
    if hit.hit then
        hit.node:take_damage(50)
        self:play_anim("recoil")
        wait(0.3)
    end
    self:play_anim("idle")
end
```

Instead, the developer must write an explicit state machine:

```lua
function M:on_update(ctx, dt)
    if self.state == "wind_up" then
        self.timer = self.timer - dt
        if self.timer <= 0 then
            self.state = "strike"
            -- ...
        end
    elseif self.state == "strike" then
        -- ...
    end
end
```

This is 3-5x more code, much harder for LLMs to generate correctly, and dramatically harder to debug.

### Technical Design

LuaJIT supports native Lua coroutines with `coroutine.create`, `coroutine.resume`, `coroutine.yield`. The implementation would:

1. **Coroutine Runner in `script_runner.cpp`**: Each node can have an active coroutine list. On each `on_update`, the runner resumes each coroutine.

2. **Yield primitives** injected as global functions or `ctx` methods:
   - `wait(seconds)` — yield, resume after N seconds (timer-backed)
   - `wait_frames(n)` — yield, resume after N frames
   - `wait_until(predicate_fn)` — yield, resume when predicate returns true
   - `wait_event(event_name)` — yield, resume when EventBus fires named event
   - `wait_signal(node, signal_name)` — yield, resume when node emits signal

3. **Coroutine lifecycle**:
   - Started via `self:start_coroutine(fn)` which returns a `coroutine_id`
   - Cancelled via `self:cancel_coroutine(coroutine_id)` or automatically on `on_stop`
   - Dead coroutines (finished or errored) are garbage collected

4. **LuaJIT consideration**: `coroutine.yield` across C call boundaries is NYI in LuaJIT's JIT compiler (traces abort). This means coroutine-heavy code runs in interpreter mode. For gameplay scripts (not inner loops), this is acceptable (~10x slower than JIT, still <1ms per frame for typical script logic). The spec must document this tradeoff explicitly.

5. **Integration with existing timer system**: `wait(seconds)` internally uses the same timer infrastructure as `schedule_after`, but the callback resumes the coroutine instead of calling a standalone function.

### Impact if Absent

- Gameplay programmers will write 3-5x more code for any sequential behavior
- AI code generation quality drops significantly (state machines are harder to generate correctly)
- Cutscene/dialogue systems become impractical in Lua (pushed to C++)
- Tween/animation sequences require manual state tracking
- UzEngine will feel dated compared to even Defold (2016) which has native coroutine support

### Complexity: **L** (2-3 weeks)

Coroutine management, yield primitive registration, integration with timer system, hot-reload considerations (coroutines must be cancelled on script reload), serialization questions (coroutines cannot be serialized across game.dll hot-reload).

### Dependencies

- Timer System (Phase 1) — yield-based wait uses timer infrastructure
- EventBus (Phase 3) — `wait_event()` requires event subscription

---

## Gap 2: Debug Adapter Protocol (DAP) for Lua Debugging in VSCode

### Why Critical

Every modern engine provides script debugging: Unreal has Visual Studio C++ debugging + Blueprint debugger. Unity has the C# debugger. Godot has a built-in GDScript debugger with breakpoints. Roblox Studio has a full Lua debugger. Defold uses the mobdebug Lua debugging protocol.

Without a debugger, developers must rely on `ctx:log()` print statements, which is the single most common complaint from Lua users in game engines. The spec mentions error reporting (file + line) but says nothing about interactive debugging.

### Current State in Spec

The spec provides:
- Error messages with file + line + stack trace
- `lua.eval` IPC for executing snippets
- `lua.get_errors` IPC for error history
- VSCode diagnostics for error annotations

None of these enable setting a breakpoint, stepping through code, or inspecting variable state interactively.

### Technical Design

Implement the Debug Adapter Protocol (DAP) as a Lua debug server inside `game.dll`:

1. **DAP Server**: A TCP server (port 12321 by default) that speaks the DAP JSON protocol. Runs on a dedicated thread inside `game.dll`. VSCode connects via a launch configuration.

2. **Breakpoint management**: Uses `lua_sethook(L, hook_fn, LUA_MASKLINE | LUA_MASKCALL | LUA_MASKRET, 0)` to intercept every line. When a line matches a registered breakpoint, the hook function:
   - Pauses the game loop (sets a flag that `uz_game_update` checks)
   - Sends a `stopped` event to VSCode via DAP
   - Enters a polling loop, processing DAP requests (continue, stepOver, stepInto, evaluate, etc.)
   - On `continue`, clears the pause flag and returns from the hook

3. **Variable inspection**: The DAP `variables` request walks the Lua stack using `lua_getlocal`, `lua_getupvalue`, and the `self` table to build a variable tree. Supports expanding tables to arbitrary depth.

4. **Conditional breakpoints**: The hook evaluates a Lua expression via `luaL_loadstring` + `lua_pcall` at the breakpoint location. If it returns truthy, the breakpoint activates.

5. **Watch expressions**: Evaluated via `lua_eval` in the context of the paused coroutine/call frame.

6. **Performance**: `lua_sethook` with `LUA_MASKLINE` disables the JIT entirely (hooks are NYI). This is acceptable for debug builds only. The debug hook must be disabled in `UZ_DEV` and `UZ_SHIPPING` builds. An alternative for hot-path debugging is to use LuaJIT's `jit.attach("trace", ...)` for trace-level introspection without disabling JIT.

7. **VSCode integration**: A `launch.json` configuration:
   ```json
   {
     "type": "uzlua",
     "request": "attach",
     "name": "Attach to UzEngine Lua",
     "port": 12321
   }
   ```

8. **Sandbox concern**: The `debug.*` library is stripped in non-debug builds. DAP requires `debug.getlocal`, `debug.getupvalue`, `debug.sethook`. The DAP server must only be compiled and linked in `UZ_DEBUG` and `UZ_EDITOR` builds.

### Impact if Absent

- Developers cannot set breakpoints or step through Lua code
- Debugging complex gameplay logic requires `ctx:log()` printf debugging
- Significantly increases time-to-fix for script bugs
- Makes UzEngine uncompetitive with Roblox Studio, Defold, and Godot for script debugging experience
- AI agents cannot provide interactive debugging assistance

### Complexity: **XL** (4+ weeks)

Full DAP protocol implementation, Lua hook management, VSCode debug extension, thread synchronization between DAP server and game loop, conditional breakpoints, watch expressions, coroutine-aware stack frames.

### Dependencies

- Lua Scripting (Phase 1) — requires functioning VM
- Console Commands (Phase 1) — useful for debug commands alongside DAP
- VSCode Extension — needs a debug adapter extension contribution point

---

## Gap 3: Integrated Lua Profiler with Per-Script Flame Graph

### Why Critical

Unity has the Profiler window with per-method timing. Unreal has Unreal Insights. Godot has the built-in profiler. Roblox has the MicroProfiler. The current spec mentions Tracy for C++ profiling (`debug-profiling.md`) but says nothing about profiling Lua script execution.

When a game runs at 30fps instead of 60fps, developers need to know: is it the renderer? Physics? Or is `enemy_ai.lua:on_update` taking 5ms per frame because of an O(n^2) loop? Without Lua-specific profiling, this is invisible.

### Current State in Spec

The `debug-profiling.md` spec covers:
- Tracy CPU/GPU timeline capture (C++ only)
- ImGui overlay with FPS counter
- Memory statistics

No mention of per-Lua-function timing, per-script execution cost, or Lua-specific memory profiling.

### Technical Design

1. **Instrumentation via `lua_sethook`**: In profiler mode (enabled via console command `lua_profile start`), install a hook with `LUA_MASKCALL | LUA_MASKRET`. On each call/return:
   - Record the function source file, line, and name
   - Record timestamp via `__rdtsc` (sub-microsecond precision)
   - Push/pop onto a thread-local call stack
   - This builds a per-frame call tree

2. **Integration with Tracy**: Each Lua function call/return emits a Tracy zone via `TracyLuaZone`. This integrates Lua profiling data directly into the Tracy timeline alongside C++ zones, giving a unified view.

3. **Lightweight sampling profiler** (alternative to hook-based): Instead of hooking every call, sample the Lua call stack every 1ms using a timer interrupt. This gives statistical profiles with minimal overhead (~2% vs ~30% for hook-based). Uses `lua_getstack` + `lua_getinfo` to capture the stack.

4. **Per-script summary**: The profiler aggregates timing data per script file and per function:
   ```
   Script                    | Calls/Frame | Avg ms | Peak ms | % Frame
   enemy_ai.lua:on_update    |     15      |  0.12  |  0.45   |  2.7%
   player.lua:on_update      |      1      |  0.08  |  0.12   |  0.4%
   door_trigger.lua:on_start |      3      |  0.01  |  0.02   |  0.1%
   ```

5. **Flame graph export**: The call tree data can be exported as a JSON suitable for speedscope or Chrome Tracing format, viewable in the browser or in a VSCode webview panel.

6. **IPC handler**: `lua.profile_start`, `lua.profile_stop`, `lua.profile_report` for programmatic access from the editor.

7. **GC profiling**: Track `lua_gc` calls, report allocation rates, and identify scripts that create excessive garbage (short-lived tables in hot loops).

### Impact if Absent

- Impossible to identify which Lua scripts are causing performance issues
- Developers must resort to manual `os.clock()` timing (blocked by sandbox!)
- Performance optimization of Lua code becomes guesswork
- No integration between C++ profiling (Tracy) and Lua execution

### Complexity: **L** (2-3 weeks)

Hook-based instrumentation, Tracy integration, data aggregation, IPC handlers, optional flame graph viewer.

### Dependencies

- Lua Scripting (Phase 1)
- Debug & Profiling (Phase 1) — Tracy infrastructure
- Console Commands (Phase 1) — profile start/stop commands

---

## Gap 4: Type-Safe Lua Bindings via C++ Reflection / Code Generation

### Why Critical

The current spec shows manual binding code (`lua_pushcfunction`, `lua_setfield`, etc.) for each C++ type exposed to Lua. This is the most error-prone part of any Lua integration:

- Adding a new field to Vec3 requires editing `lua_binding.cpp` in 3+ places
- Forgetting to update bindings after a C++ API change causes silent runtime errors
- No compile-time verification that Lua bindings match C++ signatures
- Maintaining bindings for 20+ types (Vec3, Quat, NodeRef, Scene, InputState, EventBus, TimerCtx, AudioSource, PhysicsBody, etc.) is a significant maintenance burden

Unreal solves this with UPROPERTY/UFUNCTION macros + UnrealHeaderTool code generation. Unity uses C# attributes + IL2CPP. Defold uses script_doc + C API generation. Roblox uses Luau type annotations + native code generation.

### Current State in Spec

Manual `lua_pushcfunction` / `lua_setfield` pattern for each binding. No code generation, no schema file, no automatic synchronization between C++ types and Lua metatables.

### Technical Design

1. **Binding DSL file** (`.uzbind`): A declarative file that describes what to expose:
   ```
   [type Vec3]
   fields = x:float, y:float, z:float
   constructors = Vec3(x, y, z)
   operators = __add, __sub, __mul(scalar), __unm, __eq, __tostring
   methods = length():float, normalized():Vec3, dot(other:Vec3):float, cross(other:Vec3):Vec3
   static_methods = lerp(a:Vec3, b:Vec3, t:float):Vec3, distance(a:Vec3, b:Vec3):float

   [type NodeRef]
   fields = name:string(readonly), id:int(readonly), position:Vec3, rotation:Vec3, scale:Vec3, enabled:bool
   methods = get_child(name:string):NodeRef?, get_parent():NodeRef?, is_valid():bool, has_tag(tag:string):bool
   ```

2. **Code generator** (`tools/gen_lua_bindings.py` or a CMake custom command): Reads `.uzbind` files and generates:
   - `lua_binding_generated.cpp` — metatable registration code
   - `lua_binding_generated.h` — type-check macros
   - `lua_api_reference.md` — auto-generated documentation
   - `uzengine.lua` — Luau/EmmyLua type annotations for IDE autocompletion

3. **Runtime type checking**: Generated push/check functions verify types at the C boundary:
   ```cpp
   // Generated code
   Vec3* check_vec3(lua_State* L, int idx) {
       return static_cast<Vec3*>(luaL_checkudata(L, idx, "Vec3"));
   }
   ```

4. **Versioning**: The generator stamps a version hash into the binding. On VM init, if the Lua-side type stubs don't match the C++ binding version, a clear error is logged.

5. **Alternative: sol2 or similar library**: Consider using sol2, sol3, or a lightweight binding library (kaguya, selene) that provides template-based binding. However, LuaJIT compatibility with sol2 requires testing — sol2 sometimes uses Lua 5.3+ features that LuaJIT (5.1-based) does not support.

### Impact if Absent

- Every new C++ type exposed to Lua requires 50-200 lines of manual binding code
- Binding bugs (wrong argument count, wrong type check) cause crashes or silent corruption
- No IDE autocompletion for Lua engine API (no type stub file)
- Documentation for Lua API must be written and maintained manually, separate from code
- AI code generation for Lua scripts lacks type information

### Complexity: **L** (2-3 weeks)

DSL design, code generator, integration into CMake build, generating type stubs for EmmyLua/Luau, testing with all existing types.

### Dependencies

- Lua Scripting (Phase 1) — binding infrastructure must exist first

---

## Gap 5: Module System (Controlled `require` with Path Resolution)

### Why Critical

The current spec strips `require` entirely from the sandbox. This means every `.lua` script is an isolated island — no code reuse between scripts. In practice, game projects have shared utility code:

```lua
-- utils/math_utils.lua — used by 15+ scripts
local M = {}
function M.clamp(x, min, max) return math.max(min, math.min(max, x)) end
function M.lerp(a, b, t) return a + (b - a) * t end
function M.remap(x, in_min, in_max, out_min, out_max)
    return out_min + (x - in_min) / (in_max - in_min) * (out_max - out_min)
end
return M
```

Without `require`, this must be copy-pasted into every script. Defold, Roblox, CryEngine, and LOVE2D all provide sandboxed `require` with controlled path resolution.

### Current State in Spec

`require` is listed as "stripped (forbidden in game scripts)." No alternative is provided.

### Technical Design

1. **Custom `require` replacement**: `uz_require(module_path)` — a Lua function registered as a global that:
   - Resolves `module_path` relative to `res://game/` (never filesystem root)
   - Checks a whitelist of allowed directories (`res://game/`, `res://game/lib/`, `res://game/utils/`)
   - Uses `luaL_loadfile` with the resolved absolute path
   - Caches the result in a module registry table (standard Lua `package.loaded` pattern)
   - Returns the cached module table on subsequent calls

2. **Path resolution rules**:
   ```lua
   -- From res://game/enemy_ai.lua:
   local utils = require("utils/math_utils")  -- resolves to res://game/utils/math_utils.lua
   local fsm   = require("lib/state_machine") -- resolves to res://game/lib/state_machine.lua
   ```

3. **Sandbox constraints**:
   - Only `.lua` files under `res://game/` can be required
   - No `..` path traversal (rejected with error)
   - No absolute paths (rejected with error)
   - The original `require`, `package.path`, `package.cpath`, `package.loaders` remain stripped
   - FFI `require("ffi")` is handled separately (allowed in debug builds only)

4. **Hot-reload integration**: When a required module changes:
   - The module cache entry is invalidated
   - All scripts that imported it via `uz_require` are reloaded (dependency tracking)
   - This requires building a dependency graph: `enemy_ai.lua` depends on `utils/math_utils.lua`

5. **Circular dependency detection**: If A requires B requires A, log an error and return `nil` for the cycle-breaking require (same behavior as standard Lua).

### Impact if Absent

- Zero code reuse between Lua scripts — every utility function is copy-pasted
- Larger scripts, more duplication, harder to maintain
- AI generates redundant code across scripts (no shared library to reference)
- Common patterns (state machines, tween libraries, math utilities) cannot be shared
- Contradicts the "AI-native" philosophy — LLMs prefer importing libraries

### Complexity: **M** (1 week)

Custom require function, path resolution, module cache, dependency tracking for hot-reload, circular dependency detection.

### Dependencies

- Lua Scripting (Phase 1) — sandbox must exist
- Hot-Reload system — dependency graph for cascading reload

---

## Gap 6: Shared State Between Scripts (Global Game State)

### Why Critical

The current spec provides per-node `self` tables with strict isolation. This is correct for most gameplay data. However, games need shared state:

- Player health, score, inventory (accessed by HUD script, enemy AI, save system)
- Quest progress flags (accessed by dialogue scripts, trigger scripts, quest tracker)
- Global difficulty settings, game mode flags
- Blackboard data for AI coordination (multiple enemies share awareness)

Unity uses static classes, singletons, or ScriptableObjects. Unreal uses GameState, GameInstance. Godot uses Autoloads (global singletons). Without shared state, scripts must communicate exclusively through the EventBus, which is verbose and loses data (events are fire-and-forget, not queryable).

### Current State in Spec

- Per-node `self` tables: fully specified
- EventBus: fire-and-forget communication (Phase 3)
- No global data store, no blackboard, no shared tables

### Technical Design

1. **`ctx.shared` table**: A global Lua table accessible from all scripts, persisted across frames, serialized across hot-reloads:
   ```lua
   -- player.lua
   function M:on_update(ctx, dt)
       ctx.shared.player_health = self.health
       ctx.shared.player_position = self.position
   end

   -- enemy_ai.lua
   function M:on_update(ctx, dt)
       local player_pos = ctx.shared.player_position
       if player_pos then
           self:navigate_to(player_pos)
       end
   end

   -- hud.lua
   function M:on_update(ctx, dt)
       self:draw_health_bar(ctx.shared.player_health or 100)
   end
   ```

2. **Namespacing**: Optionally support `ctx.shared:namespace("ai")` which returns a sub-table, preventing key collisions between unrelated systems.

3. **Read/write access control**: In editor builds, optionally log warnings when a shared key is written by multiple scripts in the same frame (potential race condition indicator).

4. **Serialization**: `ctx.shared` is included in the `on_save_state` / `on_load_state` msgpack blob, surviving game.dll hot-reloads. Only serializable types (numbers, strings, booleans, nested tables) are preserved.

5. **Blackboard pattern for AI**: A specialized `ctx.blackboard` (or `ctx.shared.blackboard`) with typed get/set:
   ```lua
   ctx.blackboard:set("player_last_seen", self.position)
   ctx.blackboard:set("alert_level", 3)
   local pos = ctx.blackboard:get("player_last_seen") -- Vec3 or nil
   ```

### Impact if Absent

- Scripts cannot share data without the EventBus (which is Phase 3 and fire-and-forget)
- Common patterns (HUD reading player health) require awkward workarounds
- AI coordination between enemies is impossible without shared state
- Save/load systems cannot access centralized game state
- Contradicts the spec's own example code (e.g., `enemy_ai.lua` references `self.target` from `ctx.scene:find_node("Player")` but has no way to read Player's actual health)

### Complexity: **S** (1-3 days)

Simple global table registration, serialization integration, optional namespace helper.

### Dependencies

- Lua Scripting (Phase 1)
- State Persistence (game.dll hot-reload serialization)

---

## Gap 7: Bytecode Compilation Cache

### Why Critical

The spec mentions pre-compiled bytecode for shipping builds (`luajit -b`), but says nothing about caching compiled bytecode during development. Every time the VM is created (game.dll hot-reload, scene load), all Lua scripts are parsed from source text. For a project with 100+ scripts, this adds measurable load time.

Defold compiles Lua to bytecode at build time. Roblox pre-compiles to Luau bytecode. CryEngine caches compiled Lua.

### Current State in Spec

- Shipping: `luajit -b` pre-compilation to `.luac` (specified in Phase 4)
- Development: `luaL_loadfile` from source text every time (no caching mentioned)

### Technical Design

1. **Development bytecode cache**: After `luaL_loadfile` succeeds, dump the compiled chunk via `lua_dump` to a cache directory (`res://game/.luacache/`).

2. **Cache invalidation**: Store the source file's last-modified timestamp and content hash (xxhash3) alongside the cached bytecode. On next load, compare — if source is unchanged, load bytecode directly via `luaL_loadbuffer` (skips parsing).

3. **Performance impact**:
   - Parsing 100 Lua files: ~50-100ms (LuaJIT parser is fast)
   - Loading 100 cached bytecode files: ~5-10ms
   - Savings: ~40-90ms on VM startup

4. **Cache location**: `<project>/.cache/luajit/` (gitignored). Files named by content hash to avoid stale entries.

5. **Integration with hot-reload**: When a file changes, its cache entry is invalidated. Dependent modules (from Gap 5) also invalidated.

### Impact if Absent

- VM startup takes longer than necessary during development
- game.dll hot-reload (which destroys/recreates VM) pays full parse cost every time
- Minor issue for small projects, significant for 100+ script projects

### Complexity: **S** (1-3 days)

`lua_dump` / `luaL_loadbuffer`, file timestamp comparison, cache directory management.

### Dependencies

- Lua Scripting (Phase 1)

---

## Gap 8: Rich Error Reporting (Local Variables, Watch Expressions, Error Context)

### Why Critical

The current error format shows file, line, and stack trace:
```
[lua error] res://game/enemy_ai.lua:14: attempt to index nil value (field 'target')
```

This is the bare minimum. Production engines provide:
- Local variable values at the error site
- Upvalue contents
- The `self` table contents (since this is the most common debugging need)
- The full expression that failed (not just "field 'target'")

Roblox's error reporting includes the full stack with local variable dumps. Defold includes type information. Without rich errors, debugging Lua scripts requires adding print statements and reproducing the error.

### Current State in Spec

- File + line + error message
- Stack traceback
- Error sent to VSCode as diagnostic annotation

Missing: local variable dump, self table snapshot, expression context.

### Technical Design

1. **Custom error handler via `xpcall`**: Instead of `lua_pcall`, use `lua_pcall` with a custom message handler (4th argument) that captures:
   ```cpp
   int error_handler(lua_State* L) {
       // 1. Get the error message
       const char* msg = lua_tostring(L, -1);

       // 2. Build rich error table
       lua_newtable(L);
       lua_pushstring(L, msg);
       lua_setfield(L, -2, "message");

       // 3. Capture stack trace with locals
       for (int level = 0; level < 10; level++) {
           lua_Debug ar;
           if (!lua_getstack(L, level, &ar)) break;
           lua_getinfo(L, "Slntu", &ar);

           // Capture locals at this level
           for (int i = 1; ; i++) {
               const char* name = lua_getlocal(L, &ar, i);
               if (!name) break;
               // Store name + value in error table
               // Use lua_type to get type, lua_tostring for representation
               lua_pop(L, 1); // pop value
           }

           // Capture upvalues
           // ...
       }
       return 1;
   }
   ```

2. **Self table snapshot**: When an error occurs in `on_update`, the error handler inspects the first argument (which is `self`) and serializes its keys/values into the error report.

3. **Error report format** (sent via IPC to VSCode):
   ```json
   {
     "file": "res://game/enemy_ai.lua",
     "line": 14,
     "message": "attempt to index nil value (field 'target')",
     "expression": "self.target.position",
     "locals": {
       "dt": 0.016,
       "dir": null
     },
     "self_snapshot": {
       "speed": 4.0,
       "health": 100,
       "target": null,
       "nav_timer": 0.2
     },
     "stack": [
       { "file": "enemy_ai.lua", "line": 14, "function": "on_update" },
       { "file": "[C]", "line": 0, "function": "?" }
     ]
   }
   ```

4. **VSCode display**: The error diagnostic shows an expandable panel with local variables and self table, not just a red squiggly line.

### Impact if Absent

- Developers see cryptic error messages without context
- Debugging requires reproducing the error with print statements added
- AI agents (Claude) cannot diagnose issues from error messages alone — they need variable state

### Complexity: **M** (1 week)

Custom error handler, local/upvalue capture, serialization to JSON, IPC transmission, VSCode panel rendering.

### Dependencies

- Lua Scripting (Phase 1)
- Console Commands (Phase 1) — rich error display in console

---

## Gap 9: Multiple VM Instances / Context Isolation

### Why Critical

The spec states "one VM per active scene, reset on scene load." This works for simple games but fails for:

- **Multiplayer / split-screen**: Two players need isolated script state
- **Editor vs. game isolation**: Editor scripts (gizmo tools, inspector plugins) should not share state with game scripts
- **Mod sandboxing**: User-created mods need their own VMs with restricted APIs
- **Testing**: Unit tests for Lua scripts need isolated VMs
- **Parallel scenes**: Loading screen logic running alongside a scene transition

Unreal has separate Worlds. Unity has separate Scenes with DontDestroyOnLoad. Roblox has separate DataModel instances per server/client.

### Current State in Spec

Single `lua_State*` per scene. No discussion of multiple VMs, editor script isolation, or mod sandboxing.

### Technical Design

1. **VM Pool**: `LuaVMManager` manages multiple `lua_State*` instances:
   ```cpp
   class LuaVMManager {
       LuaVM game_vm_;          // main game scripts
       LuaVM editor_vm_;        // editor tool scripts (gizmos, inspectors)
       LuaVM mod_vm_;           // user mods (restricted sandbox)
       LuaVM test_vm_;          // unit test runner
   };
   ```

2. **Per-VM sandbox levels**: Each VM can have a different sandbox configuration:
   - Game VM: standard sandbox (no io, os, require, debug)
   - Editor VM: relaxed sandbox (allows file read for tool scripts, debug.* always available)
   - Mod VM: strict sandbox (no FFI, no raw memory access, memory limit via `lua_gc`)
   - Test VM: full access (debug.*, custom assertions)

3. **VM communication**: VMs cannot share `lua_State*` tables directly. Communication happens through:
   - The shared C++ layer (EventBus bridges)
   - Serialized messages (msgpack tables passed between VMs)
   - Shared C++ data structures (read-only from Lua)

4. **Memory budgets**: Each VM has a configurable memory limit enforced via a custom allocator passed to `lua_newstate`:
   ```cpp
   lua_State* L = lua_newstate(budget_allocator, &budget);
   // budget_allocator returns NULL when budget exceeded
   ```

### Impact if Absent

- No isolation between game and editor scripts
- No mod support (or mods can crash the engine)
- No parallel scene loading with independent script state
- Testing Lua scripts requires the full game VM (no isolated test environment)

### Complexity: **L** (2-3 weeks)

VM manager, per-VM sandbox configuration, inter-VM communication, memory budgets, integration with existing single-VM architecture.

### Dependencies

- Lua Scripting (Phase 1)
- Plugin System (Phase 4) — mods are a form of plugin

---

## Gap 10: Configurable Sandbox Levels

### Why Critical

The spec has a single, fixed sandbox configuration. But different contexts need different access:

| Context | Needs |
|---------|-------|
| Game scripts (shipping) | Strict: no io, no os, no debug, no FFI |
| Game scripts (editor) | Standard: no io/os, debug.* available |
| Editor tool scripts | Relaxed: file read (for custom importers), debug.* |
| Console `lua.eval` | Full: everything available (for live debugging) |
| Mod scripts | Strict + memory limit + instruction count limit |
| Test scripts | Full: everything + test framework assertions |

Without configurable sandboxing, either all scripts are over-restricted (editor tools cannot read files) or under-restricted (game scripts have debug access in shipping).

### Current State in Spec

Single sandbox applied via `lua_sandbox_apply()` after `luaL_openlibs()`. Only variation: `debug.*` kept in `UZ_DEBUG` builds.

### Technical Design

1. **Sandbox levels enum**:
   ```cpp
   enum class SandboxLevel : uint8_t {
       FULL,        // Everything available (console eval, tests)
       EDITOR,      // io.read (no write), debug.*, no os.execute
       STANDARD,    // No io, no os, debug.* in debug builds only
       STRICT,      // No io, no os, no debug, no FFI, no rawset/rawget
       MODDED       // STRICT + memory limit + instruction count limit
   };
   ```

2. **Per-level configuration**:
   ```cpp
   void lua_sandbox_apply(lua_State* L, SandboxLevel level) {
       switch (level) {
           case SandboxLevel::MODDED:
               strip_rawaccess(L);  // rawget, rawset, setmetatable
               set_instruction_limit(L, 1000000);  // via debug.sethook count
               set_memory_limit(L, 64 * 1024 * 1024);  // 64 MB
               [[fallthrough]];
           case SandboxLevel::STRICT:
               strip_ffi(L);
               strip_debug(L);
               [[fallthrough]];
           case SandboxLevel::STANDARD:
               strip_io(L);
               strip_os(L);
               strip_require(L);
               strip_loadfile(L);
               break;
           case SandboxLevel::EDITOR:
               strip_os_execute(L);  // keep os.clock, os.time
               strip_io_write(L);    // keep io.read, io.open("r")
               break;
           case SandboxLevel::FULL:
               break;  // everything available
       }
   }
   ```

3. **Instruction count limit** (for mods): Uses `lua_sethook(L, hook, LUA_MASKCOUNT, limit)`. When the instruction count exceeds the limit, the hook calls `lua_error` with "script exceeded instruction limit." This prevents infinite loops from freezing the engine.

### Impact if Absent

- Editor tool scripts cannot access files (over-restricted)
- No mod support without per-mod sandboxing
- No instruction count limits — malicious or buggy mods can freeze the engine
- Console `lua.eval` has the same restrictions as game scripts (under-powered for debugging)

### Complexity: **M** (1 week)

Sandbox level design, per-level stripping functions, instruction count hook, memory limit allocator.

### Dependencies

- Lua Scripting (Phase 1)
- Multiple VM Instances (Gap 9) — each VM needs its own sandbox level

---

## Gap 11: Lua GC Tuning API

### Why Critical

LuaJIT uses an incremental mark-and-sweep garbage collector. The default tuning is acceptable for general use, but games have specific GC requirements:

- GC pauses must not exceed the frame budget (16.6ms at 60fps)
- Allocation-heavy frames (scene load, particle burst) can trigger long GC pauses
- Some games prefer to run GC during loading screens or between levels

Roblox exposes GC tuning. Unity's C# GC is a major pain point — UzEngine should not repeat this mistake. The console `gc` command exists but offers no tuning.

### Current State in Spec

- Console command `gc` forces a full collection
- No API to tune GC parameters, no incremental budget, no per-frame stepping

### Technical Design

1. **Lua-facing GC API** on `ctx`:
   ```lua
   ctx.gc:set_pause(200)        -- GC pause parameter (default 200)
   ctx.gc:set_stepmul(200)      -- GC step multiplier (default 200)
   ctx.gc:step(budget_kb)       -- Run incremental GC step with KB budget
   ctx.gc:collect()             -- Full collection (equivalent to console 'gc')
   ctx.gc:count()               -- Returns memory in use (KB)
   ctx.gc:stop()                -- Disable automatic GC (manual only)
   ctx.gc:restart()             -- Re-enable automatic GC
   ```

2. **Per-frame incremental stepping**: In `uz_game_update`, after all `on_update` calls, run a small GC step:
   ```cpp
   // lua_vm.cpp — after drain_timers + on_update dispatch
   if (gc_mode_ == GCMode::INCREMENTAL) {
       lua_gc(L, LUA_GCSTEP, gc_step_budget_kb_);
   }
   ```

3. **Frame-budget-aware GC**: Measure time remaining in the frame budget. If >2ms remain, run a GC step. If <2ms, skip GC this frame.

4. **GC statistics IPC**: `lua.gc_stats` returns allocation rate, collection time, memory usage — displayed in the profiler overlay.

### Impact if Absent

- GC pauses cause frame spikes that are invisible and uncontrollable
- No way to schedule GC during non-critical moments (loading screens, menus)
- Developers cannot diagnose memory-related performance issues in Lua

### Complexity: **S** (1-3 days)

Thin wrapper around `lua_gc` with parameter passing, per-frame step integration, IPC stats handler.

### Dependencies

- Lua Scripting (Phase 1)

---

## Gap 12: Safe FFI for Accessing C++ Structs from Lua

### Why Critical

The spec mentions LuaJIT FFI but restricts it to `UZ_DEBUG` and `UZ_DEV` builds. In production, scripts cannot use FFI at all. This means all C++ data access goes through the Lua C API (push/check functions), which has overhead:

- Each `node.position` read: C function call, stack manipulation, userdata allocation
- In a loop iterating 100 entities: 100x the overhead of direct struct access

LuaJIT FFI allows zero-overhead access to C structs from Lua, matching C performance. Removing it in shipping eliminates a major performance tool.

### Current State in Spec

- FFI available in debug/dev builds only
- Stripped in shipping: `require("ffi")` returns nil
- Scripts must check and fall back to engine API

### Technical Design

1. **Curated FFI definitions**: Instead of allowing arbitrary `ffi.cdef`, provide a pre-defined set of FFI struct definitions that mirror the engine API:
   ```lua
   -- Automatically injected into VM (not user-defined)
   -- Engine maintains these definitions in sync with C++ headers
   local ffi = require("ffi")
   ffi.cdef [[
       typedef struct { float x, y, z; } uz_Vec3;
       typedef struct { float x, y, z, w; } uz_Quat;
       typedef struct {
           uz_Vec3 position;
           uz_Quat rotation;
           uz_Vec3 scale;
       } uz_Transform;
   ]]
   ```

2. **Read-only FFI access**: Expose C++ data as `const` pointers via FFI. Lua can read struct fields at zero cost but cannot write directly (writes go through the safe C API that enqueues SceneCommands).

3. **Shipping FFI whitelist**: Instead of stripping FFI entirely, allow a restricted subset:
   - Only engine-defined `cdef` blocks (no user-defined)
   - Only read access to whitelisted struct types
   - No `ffi.C` function calls (only struct field access)
   - No `ffi.new` for allocation (only engine-provided pointers)

4. **Performance impact**: For scripts that iterate over many entities (e.g., spatial queries, batch operations), FFI struct access is 10-50x faster than the Lua C API path.

### Impact if Absent

- Lua scripts in shipping builds are 10-50x slower for struct field access compared to FFI
- Performance-critical Lua code must be pushed to C++ (defeating the purpose of scripting)
- Dev/debug builds perform differently from shipping (potential for shipping-only bugs)

### Complexity: **M** (1 week)

Curated FFI definitions, read-only access enforcement, shipping-safe FFI subset, synchronization between C++ structs and FFI definitions.

### Dependencies

- Lua Scripting (Phase 1)
- Type-Safe Bindings (Gap 4) — FFI definitions should be auto-generated from the same source

---

## Gap 13: Lua Test Framework

### Why Critical

The spec requires 95% test coverage for C++ code. But there is no mention of testing Lua scripts. In a production engine, Lua scripts contain significant game logic — untested Lua is untested game logic.

Defold has a test runner for Lua. Roblox has TestService. Unity has Play Mode tests. Without a Lua test framework, the only way to verify script behavior is to load a scene, run the game, and manually observe.

### Current State in Spec

No Lua testing infrastructure mentioned. The C++ test framework (doctest/Google Test) tests the binding layer but cannot test Lua script logic.

### Technical Design

1. **Test runner**: A special `test_vm` (from Gap 9) that loads test files:
   ```lua
   -- tests/test_enemy_ai.lua
   local T = {}
   local enemy_ai = require("enemy_ai")  -- requires Gap 5 module system

   function T:test_initial_health()
       local inst = create_test_instance(enemy_ai)
       local ctx = create_mock_context()
       inst:on_start(ctx)
       assert_equal(inst.health, 100, "Initial health should be 100")
   end

   function T:test_navigation_timer()
       local inst = create_test_instance(enemy_ai)
       local ctx = create_mock_context()
       inst:on_start(ctx)
       inst:on_update(ctx, 0.1)
       assert_equal(inst.nav_timer, 0.1, "Nav timer should accumulate dt")
   end

   return T
   ```

2. **Assertion library**:
   ```lua
   assert_equal(actual, expected, message)
   assert_near(actual, expected, tolerance, message)
   assert_true(value, message)
   assert_false(value, message)
   assert_nil(value, message)
   assert_not_nil(value, message)
   assert_error(fn, message)  -- expects fn to throw
   assert_type(value, type_name, message)
   ```

3. **Mock context factory**: `create_mock_context()` returns a fake `ctx` with:
   - Stub scene (configurable nodes)
   - Stub input (configurable pressed/held state)
   - Controllable time (manual dt advancement)
   - Event capture (records emitted events for assertion)

4. **Console command**: `run_lua_tests [pattern]` — discovers and runs test files matching the pattern.

5. **IPC handler**: `lua.run_tests` — returns test results as JSON for VSCode Test Explorer integration.

6. **Coverage**: Use `lua_sethook` with `LUA_MASKLINE` to track which lines are executed during tests. Report coverage percentage per script file.

### Impact if Absent

- Lua game logic is untested (only manually verified)
- Regressions in script behavior are caught only by playtesting
- Cannot maintain the 95% coverage target claimed in CLAUDE.md
- AI-generated scripts have no automated verification

### Complexity: **M** (1 week)

Assertion library, mock context, test runner, console command, IPC handler, coverage tracking.

### Dependencies

- Lua Scripting (Phase 1)
- Module System (Gap 5) — tests need to `require` the module under test
- Multiple VM Instances (Gap 9) — tests should run in isolated VM

---

## Gap 14: Script Templates and Snippets in Editor

### Why Critical

When a developer attaches a new script to a node, they need to start from a template. Without templates, every new script starts from a blank file, and the developer must remember the module pattern, lifecycle method signatures, and available `ctx` APIs.

Unreal has Blueprint templates. Unity has MonoBehaviour script templates. Godot has script templates for each node type. Roblox has starter scripts.

### Current State in Spec

The `ai-native-features.md` spec has `script_generate` which uses an LLM to generate scripts. But there is no offline, non-AI-dependent template system.

### Technical Design

1. **Template files**: `res://game/.templates/` directory with `.lua.template` files:
   ```lua
   -- templates/node3d_basic.lua.template
   -- Template: Basic Node3D Script
   -- Description: A simple script with movement and input handling
   local M = {}

   function M:on_start(ctx)
       self.speed = ${speed:5.0}
   end

   function M:on_update(ctx, dt)
       if ctx.input:held("${move_action:MoveForward}") then
           local forward = self.rotation * Vec3(0, 0, 1)
           self.position = self.position + forward * self.speed * dt
       end
   end

   function M:on_stop(ctx)
   end

   return M
   ```

2. **Template variables**: `${name:default}` syntax, filled in by a dialog or command-line prompt.

3. **Node-type-aware templates**: When attaching a script to a DirectionalLight, offer light-specific templates. When attaching to a Camera, offer camera templates.

4. **VSCode integration**: When using "UzEngine: Create Script" command:
   - Show a QuickPick list of available templates
   - Fill template variables via input boxes
   - Save to file, attach to selected node

5. **Snippet library**: VSCode snippet contributions in the extension's `package.json`:
   ```json
   {
     "Lua Module": {
       "prefix": "uzmod",
       "body": ["local M = {}", "", "function M:on_start(ctx)", "    $0", "end", "", "return M"]
     },
     "Schedule Timer": {
       "prefix": "uztimer",
       "body": ["self.${1:timer_id} = ctx:schedule_after(${2:1.0}, function()", "    $0", "end)"]
     }
   }
   ```

### Impact if Absent

- New script files require remembering the module pattern boilerplate
- Slower onboarding for new developers
- AI script generation (Gap in `ai-native-features.md`) is the only way to scaffold — fails offline

### Complexity: **S** (1-3 days)

Template files, variable substitution, VSCode QuickPick integration, snippet definitions.

### Dependencies

- Lua Scripting (Phase 1)
- Editor Panels (Phase 1) — script picker UI

---

## Gap 15: Visual Scripting as Alternative to Lua

### Why Critical

Not all game designers can write Lua. Visual scripting (node-based programming) is the primary authoring method for:
- Level designers (Unreal Blueprints are the industry standard)
- Technical artists (shader graphs, animation state machines)
- Non-programmers prototyping gameplay

Unreal has Blueprints. Unity has Visual Scripting (Bolt). Godot has VisualScript (deprecated but widely used). CryEngine has Schematyc. This is the single most impactful feature for broadening UzEngine's audience.

### Current State in Spec

No mention of visual scripting anywhere in the documentation. The vision doc mentions "AI-first" and "text-driven" but ignores non-programmers.

### Technical Design

1. **Visual script format**: `.uzvs` files (JSON-based node graph):
   ```json
   {
     "version": 1,
     "nodes": [
       { "id": 1, "type": "event_on_start", "position": [100, 100] },
       { "id": 2, "type": "set_variable", "position": [300, 100], "data": { "name": "speed", "value": 5.0 } },
       { "id": 3, "type": "event_on_update", "position": [100, 300] },
       { "id": 4, "type": "get_input_held", "position": [300, 300], "data": { "action": "MoveForward" } }
     ],
     "connections": [
       { "from": [1, "exec_out"], "to": [2, "exec_in"] },
       { "from": [3, "exec_out"], "to": [4, "exec_in"] }
     ]
   }
   ```

2. **Transpiler**: `.uzvs` is transpiled to `.lua` at edit time (not runtime). The generated Lua follows the standard module pattern and runs through the normal LuaJIT pipeline. This means:
   - No new runtime needed — visual scripts are Lua scripts
   - Hot-reload works identically
   - Performance is identical (it's the same Lua)
   - The generated `.lua` can be inspected, edited, and used as a learning tool

3. **Editor**: A VSCode WebView panel with a node-based graph editor (using a library like rete.js, litegraph.js, or custom Canvas rendering).

4. **Node categories**: On Start, On Update, On Stop, Get/Set Variable, Math Operations, Scene Queries, Input Checks, Timer Schedule, Event Emit/Subscribe, Branch, Loop, Sequence.

5. **Bidirectional**: A developer can start with visual scripting and "eject" to Lua when they need more control. The generated Lua is clean and readable.

### Impact if Absent

- UzEngine is Lua-only, excluding non-programmers
- Level designers cannot create gameplay without a programmer
- Significant competitive disadvantage vs. Unreal (Blueprints), Unity (Visual Scripting)
- The "AI-native" philosophy partially mitigates this (AI can write Lua for non-coders), but visual scripting is still more intuitive for many users

### Complexity: **XL** (4+ weeks)

Node graph editor UI, node type library, transpiler to Lua, bidirectional sync, VSCode WebView panel, testing.

### Dependencies

- Lua Scripting (Phase 1) — visual scripts compile to Lua
- Editor Panels (Phase 1) — WebView infrastructure
- Module System (Gap 5) — generated Lua may need to import utilities

---

## Gap 16: Serialization of Lua Closures and Functions (Save/Load State)

### Why Critical

The spec states that `self` table values are serialized via msgpack for game.dll hot-reload. But it explicitly limits serialization to "numbers, strings, booleans, nested tables of those." This means:

- **Timer callbacks are lost** on game.dll hot-reload (spec acknowledges this)
- **EventBus subscriptions are lost**
- **Coroutine state is lost** (if Gap 1 is implemented)
- **Any function reference in `self` is lost**

For save/load systems (`save-system.md`), the same limitation applies: saving a game cannot preserve timer state, active coroutines, or function references. This means game saves are incomplete — loading a save restarts all timers, cancels all coroutines, and loses all pending callbacks.

### Current State in Spec

- Only primitive types + tables serialized
- Functions, userdata, threads (coroutines) explicitly excluded
- `save-system.md` specifies msgpack for saves but does not address function serialization

### Technical Design

1. **Function identity serialization**: Instead of serializing the function bytecode (which is fragile and version-dependent), serialize a **function path** — a stable identifier that can re-resolve the function after deserialization:
   ```
   Serialized: { "type": "function_ref", "module": "enemy_ai", "method": "on_regen_tick" }
   Deserialized: enemy_ai_module_table.on_regen_tick
   ```

2. **Closure upvalue capture**: For closures that capture `self`, serialize the upvalue bindings:
   ```lua
   -- Original:
   ctx:schedule_every(2.0, function() self:regen(ctx) end)

   -- Serialized timer:
   { "timer_id": 42, "remaining": 1.3, "interval": 2.0,
     "callback": { "type": "method_call", "self_ref": "node_5", "method": "regen" } }
   ```

3. **Limitations**: General closures with arbitrary upvalues cannot be serialized. Only these patterns are supported:
   - Named module methods: `M.some_function`
   - Method calls with self: `function() self:method(ctx) end`
   - Simple value captures: `function() return self.value end`

4. **Coroutine serialization**: Coroutines cannot be serialized (LuaJIT does not support it). On save/load, active coroutines are restarted from `on_start`. The spec must document this limitation clearly.

5. **Timer serialization**: Timers can be serialized by storing:
   - Remaining time
   - Interval (for repeating timers)
   - Callback identity (function path or method reference)
   - Associated node ID

### Impact if Absent

- Game saves do not preserve timer state — active timers restart on load
- Coroutines reset on save/load — multi-frame sequences restart
- game.dll hot-reload loses more state than necessary
- Complex gameplay state (quest timers, cooldowns, scheduled events) is fragile

### Complexity: **L** (2-3 weeks)

Function identity scheme, upvalue analysis, timer serialization, integration with save system, extensive edge case testing.

### Dependencies

- Lua Scripting (Phase 1)
- Timer System (Phase 1) — timer serialization
- Save System (Phase 3) — save/load integration
- Coroutines (Gap 1) — coroutine restart-from-start behavior

---

## Gap 17: TypeScript-to-Lua Transpiler Support

### Why Critical

TypeScript provides type safety, IDE autocompletion, refactoring, and error detection at compile time — everything Lua lacks. Several production engines and frameworks support TypeScript as a scripting language that compiles to Lua:

- **Roblox**: Luau has built-in type annotations (inspired by TypeScript)
- **Defold**: Community ts-defold project transpiles TypeScript to Lua
- **LOVE2D**: TypeScriptToLua project provides full TS-to-Lua transpilation
- **Cocos Creator**: Uses TypeScript as primary scripting language

The UzEngine vision doc emphasizes "AI-first" development. LLMs generate significantly better TypeScript than Lua because:
- TypeScript has more training data
- Type annotations provide constraints that reduce hallucinations
- Import/export system is more explicit than Lua's module pattern
- IDE tooling (autocomplete, refactoring) is vastly superior

### Current State in Spec

No mention of TypeScript anywhere. Lua is the only scripting option.

### Technical Design

1. **Transpiler**: Use TypeScriptToLua (TSTL), an existing MIT-licensed project that compiles TypeScript to Lua 5.1 (LuaJIT-compatible). The UzEngine build pipeline would:
   - Watch `.ts` files in `res://game/`
   - Transpile to `.lua` in a shadow directory
   - The shadow `.lua` files are loaded by the LuaJIT VM (unchanged pipeline)

2. **Type definitions**: Generate a `uzengine.d.ts` file that describes the engine API:
   ```typescript
   // uzengine.d.ts (auto-generated from .uzbind files, see Gap 4)
   declare interface Vec3 {
       x: number; y: number; z: number;
       length(): number;
       normalized(): Vec3;
       dot(other: Vec3): number;
       cross(other: Vec3): Vec3;
   }
   declare function Vec3(x: number, y: number, z: number): Vec3;

   declare interface GameContext {
       delta_time: number;
       time_elapsed: number;
       frame_index: number;
       scene: Scene;
       input: InputState;
       log(msg: string): void;
       schedule_after(delay: number, callback: () => void): number;
       schedule_every(interval: number, callback: () => void): number;
       cancel_timer(id: number): void;
   }

   declare interface ScriptModule {
       on_start?(this: any, ctx: GameContext): void;
       on_update?(this: any, ctx: GameContext, dt: number): void;
       on_stop?(this: any, ctx: GameContext): void;
   }
   ```

3. **Workflow**:
   ```
   Developer writes: res://game/enemy_ai.ts
   TSTL transpiles:  res://game/.build/enemy_ai.lua
   LuaJIT loads:     res://game/.build/enemy_ai.lua
   Hot-reload:       watches .ts files, retranspiles, triggers Lua hot-reload
   ```

4. **Source maps**: TSTL generates source maps. Error messages reference `.ts` file and line, not the generated `.lua`.

5. **Optional**: Developers can mix `.ts` and `.lua` files in the same project. TypeScript scripts can import Lua modules (via type declarations) and vice versa.

### Impact if Absent

- No type safety for script code
- No IDE autocompletion beyond basic Lua (without EmmyLua annotations)
- AI code generation is limited to Lua (lower quality than TypeScript generation)
- Larger teams cannot enforce code quality through type checking
- UzEngine misses the TypeScript trend that Roblox, Defold, and Cocos are following

### Complexity: **L** (2-3 weeks)

TSTL integration, type definition generation, source map support, file watcher for .ts, hot-reload integration, VSCode tsconfig.json configuration.

### Dependencies

- Lua Scripting (Phase 1) — transpiled output is standard Lua
- Type-Safe Bindings (Gap 4) — type definitions generated from same source
- Module System (Gap 5) — TypeScript import/export maps to Lua require

---

## Priority Matrix

| # | Gap | Complexity | Priority | Phase |
|---|-----|-----------|----------|-------|
| 1 | Coroutines / Async | L | **P0 — Critical** | Phase 1 |
| 5 | Module System (require) | M | **P0 — Critical** | Phase 1 |
| 6 | Shared State (ctx.shared) | S | **P0 — Critical** | Phase 1 |
| 8 | Rich Error Reporting | M | **P0 — Critical** | Phase 1 |
| 11 | GC Tuning API | S | **P1 — High** | Phase 1 |
| 7 | Bytecode Cache | S | **P1 — High** | Phase 1 |
| 14 | Script Templates | S | **P1 — High** | Phase 1 |
| 4 | Type-Safe Bindings | L | **P1 — High** | Phase 2 |
| 13 | Lua Test Framework | M | **P1 — High** | Phase 2 |
| 3 | Lua Profiler | L | **P1 — High** | Phase 2 |
| 10 | Configurable Sandbox | M | **P2 — Medium** | Phase 2 |
| 12 | Safe FFI | M | **P2 — Medium** | Phase 3 |
| 16 | Function Serialization | L | **P2 — Medium** | Phase 3 |
| 2 | DAP Debugger | XL | **P2 — Medium** | Phase 2-3 |
| 9 | Multiple VM Instances | L | **P2 — Medium** | Phase 3 |
| 17 | TypeScript-to-Lua | L | **P3 — Nice-to-have** | Phase 4 |
| 15 | Visual Scripting | XL | **P3 — Nice-to-have** | Phase 4+ |

---

## Dependency Graph

```
Phase 1 (Must-Have)
  ├── Coroutines (Gap 1) ← Timer System
  ├── Module System (Gap 5) ← Lua Scripting
  ├── Shared State (Gap 6) ← Lua Scripting
  ├── Rich Error Reporting (Gap 8) ← Lua Scripting
  ├── GC Tuning (Gap 11) ← Lua Scripting
  ├── Bytecode Cache (Gap 7) ← Lua Scripting
  └── Script Templates (Gap 14) ← Editor Panels

Phase 2 (High Priority)
  ├── Type-Safe Bindings (Gap 4) ← Lua Scripting
  ├── Lua Test Framework (Gap 13) ← Module System (Gap 5), Multiple VMs (Gap 9)
  ├── Lua Profiler (Gap 3) ← Debug & Profiling, Lua Scripting
  ├── Configurable Sandbox (Gap 10) ← Lua Scripting
  └── DAP Debugger (Gap 2) ← Lua Scripting, VSCode Extension

Phase 3 (Medium Priority)
  ├── Safe FFI (Gap 12) ← Type-Safe Bindings (Gap 4)
  ├── Function Serialization (Gap 16) ← Timer System, Save System
  └── Multiple VM Instances (Gap 9) ← Lua Scripting

Phase 4 (Nice-to-Have)
  ├── TypeScript-to-Lua (Gap 17) ← Module System (Gap 5), Type-Safe Bindings (Gap 4)
  └── Visual Scripting (Gap 15) ← Lua Scripting, Editor Panels
```

---

## Total Effort Estimate

| Complexity | Count | Estimated Days |
|-----------|-------|---------------|
| S (1-3 days) | 4 | 8-12 |
| M (1 week) | 5 | 25-35 |
| L (2-3 weeks) | 6 | 72-108 |
| XL (4+ weeks) | 2 | 40-60 |
| **Total** | **17** | **145-215 person-days** |

This represents approximately 6-9 person-months of work for one Lua scripting engineer. The P0 items (Gaps 1, 5, 6, 8) should be addressed alongside the Phase 1 Lua Scripting task, adding approximately 3-4 weeks to the original estimate.

---

## Recommendations

1. **Immediately add Gaps 1, 5, 6, 8 to the Phase 1 Lua Scripting task**. These are not optional — any Lua scripting system without coroutines, module imports, shared state, and rich errors is incomplete for even basic game development.

2. **Add Gap 11 (GC Tuning) and Gap 7 (Bytecode Cache) as Phase 1 stretch goals**. They are small (S complexity) and prevent performance issues from becoming debugging mysteries.

3. **Plan Gap 4 (Type-Safe Bindings) for early Phase 2**. As the number of bound types grows (Vec3, Quat, NodeRef, Scene, InputState, EventBus, AudioSource, PhysicsBody, etc.), manual bindings become the #1 source of bugs and maintenance overhead.

4. **Plan Gap 2 (DAP Debugger) as a Phase 2-3 investment**. It is XL complexity but dramatically improves the developer experience. Consider a phased approach: Phase 2 delivers breakpoints + step + locals, Phase 3 adds conditional breakpoints + watch expressions.

5. **Defer Gaps 15 (Visual Scripting) and 17 (TypeScript-to-Lua) to Phase 4+**. They are high-value but high-effort, and the AI-native philosophy partially compensates (AI writes Lua for non-programmers).

6. **Update the `lua-scripting.md` spec** to acknowledge these gaps and mark them as planned future work, so that the Phase 1 implementation does not make architectural decisions that block future additions (e.g., hardcoding a single VM, not providing hook points for debugger attachment).
