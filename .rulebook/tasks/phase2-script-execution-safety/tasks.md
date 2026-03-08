## 1. Instruction Budget Hook

- [x] 1.1 Call `lua_sethook(L, uz_instruction_hook, LUA_MASKCOUNT, 5000000)` in `src/engine/scripting/lua_vm.cpp` immediately after creating the Lua state; the hook function calls `luaL_error(L, "script exceeded instruction budget")` to abort the script cleanly via `lua_pcall`'s error path
- [x] 1.2 Read the instruction budget from the `.uzproj` `[scripting]` section key `instruction_budget` (default `5000000`) so project authors can tighten it for simple projects or widen it for computationally heavy scripts
- [x] 1.3 Reset the instruction counter at the start of each `on_update` call by calling `lua_sethook` again with the same count, preventing leftover counts from the previous frame from causing a spurious early abort

## 2. Watchdog Thread

- [x] 2.1 Create `src/engine/scripting/script_watchdog.h` and `src/engine/scripting/script_watchdog.cpp` implementing `ScriptWatchdog` with `Start(lua_State*, timeout_ms)` and `Stop()` methods; the watchdog runs a background `std::thread` that sleeps for `timeout_ms` then checks if the guarded script call has completed
- [x] 2.2 Implement the timeout enforcement in `ScriptWatchdog`: if the script has not completed within `timeout_ms` (default 100ms from `[scripting] timeout_ms`), call `lua_sethook(L, immediate_abort_hook, LUA_MASKCALL | LUA_MASKRET | LUA_MASKCOUNT, 1)` to force the next Lua instruction to raise an error
- [x] 2.3 Pair every `lua_pcall` in `src/engine/scripting/script_runner.cpp` with a `ScriptWatchdog::Start()` before the call and `ScriptWatchdog::Stop()` after it returns; use RAII via `ScriptWatchdogGuard` to ensure `Stop()` is always called even on exceptions

## 3. Memory Allocator Cap

- [x] 3.1 Replace the default Lua allocator in `src/engine/scripting/lua_vm.cpp` with a custom allocator passed to `lua_newstate()` that tracks total allocated bytes in an `std::atomic<size_t>` per VM instance
- [x] 3.2 Implement the custom allocator to return `nullptr` (triggering a Lua memory error) when total allocated bytes would exceed the limit read from `.uzproj` `[scripting] memory_limit_mb` (default `32`)
- [x] 3.3 Expose `ctx.scripting.memory_used_bytes` as a readable value from Lua so scripts can check their own memory usage; also expose it via `engine.get_scripting_stats` IPC handler

## 4. Error Recovery

- [x] 4.1 Wrap all script calls in `src/engine/scripting/script_runner.cpp` with `lua_pcall`; on error, extract the error message using `lua_tostring(L, -1)`, store it in a per-script `ScriptErrorState`, and mark the script as `ERRORED`
- [x] 4.2 Implement skip logic in `script_runner.cpp`: scripts in `ERRORED` state are not invoked on subsequent frames until the source file changes on disk (detected by `FileWatcher`), at which point the script is hot-reloaded and the error state is cleared
- [x] 4.3 Log the error message, source file, and line number using `UZ_LOG_ERROR` each time a script enters `ERRORED` state; do not spam logs on every frame while the script remains errored

## 5. VSCode Diagnostics via IPC

- [x] 5.1 Add `script.error_report` IPC push notification sent from `script_runner.cpp` immediately after a script enters `ERRORED` state; the JSON payload contains `{"file": "scripts/enemy.lua", "line": 42, "message": "script exceeded instruction budget"}`
- [x] 5.2 Implement the VSCode extension IPC listener for `script.error_report` that calls `vscode.languages.createDiagnostic()` to place a red squiggly on the reported line in the Problems panel
- [x] 5.3 Clear the VSCode diagnostic for a script file when `script_runner.cpp` emits a `script.error_cleared` IPC event after successful hot-reload

## 6. Sandbox Escape Detection

- [x] 6.1 Install a `__newindex` metamethod on the `_G` global table in `src/engine/scripting/lua_vm.cpp` using `lua_setmetatable`; the metamethod logs `UZ_LOG_WARN("Script attempted to set global '%s' -- blocked", key)` and does not perform the assignment
- [x] 6.2 Read the sandbox escape policy from `.uzproj` `[scripting] sandbox_globals` (default `"block"`); when set to `"warn"` allow the assignment but still log; when `"block"` reject and log
- [x] 6.3 Whitelist a set of allowed global modifications specified in `.uzproj` `[scripting] allowed_globals` (e.g. `["debug_flag"]`) that bypass the `__newindex` block

## 7. Project Configuration

- [x] 7.1 Add `[scripting]` section parsing to the project loader (`src/engine/core/project_loader.cpp`) reading: `instruction_budget` (int), `timeout_ms` (int), `memory_limit_mb` (int), `sandbox_globals` (string), and `allowed_globals` (array of strings)
- [x] 7.2 Document the `[scripting]` TOML format in `docs/specs/scripting/script-execution-safety.md` with default values, valid ranges, and examples

## 8. Testing

- [x] 8.1 Create `tests/scripting/test_script_execution_safety.cpp` with fixture that initializes a `LuaVM` in isolation using the `NullAudioEngine` pattern (no hardware dependencies)
- [x] 8.2 Test that a Lua script running `while true do end` is aborted by the instruction hook within 50ms and `lua_pcall` returns `LUA_ERRRUN`
- [x] 8.3 Test that the watchdog aborts a script sleeping in `os.clock` spin for 200ms when the timeout is configured to 100ms
- [x] 8.4 Test that a normal script completing in 1ms is NOT aborted by either the hook or the watchdog
- [x] 8.5 Test memory limit: a Lua script calling `string.rep("x", 1024*1024*33)` (33 MB) is rejected by the custom allocator and `lua_pcall` returns `LUA_ERRMEM`
- [x] 8.6 Test error recovery: after a script errors, verify it is skipped on the next 3 `RunUpdate` calls; simulate a file change event and verify it is executed again on the subsequent call
- [x] 8.7 Test `script.error_report` IPC push is emitted with the correct file path, line number, and error message string
- [x] 8.8 Test `script.error_cleared` IPC push is emitted after successful hot-reload following an error
- [x] 8.9 Test sandbox global block: `_G.new_global = 1` in a Lua script triggers the warn log and the assignment is rejected; verify `rawget(_G, "new_global")` returns `nil` after the script runs
- [x] 8.10 Test whitelisted globals: with `allowed_globals = ["my_flag"]`, setting `_G.my_flag = true` is permitted and `rawget(_G, "my_flag")` returns `true`
- [x] 8.11 Test `[scripting]` TOML loading: verify `instruction_budget`, `timeout_ms`, `memory_limit_mb` are applied from the project config
- [x] 8.12 Run coverage -- all new files in `src/engine/scripting/` must reach >= 95% line and branch coverage before marking complete
