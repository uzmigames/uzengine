## 1. Prerequisites

- [ ] 1.1 Confirm `phase1-lua-scripting` is complete — LuaVM and script_runner exist
- [ ] 1.2 Confirm `phase0-debug-infrastructure-design` spec is complete — DAP-over-IPC bridge protocol defined
- [ ] 1.3 Read `docs/specs/debug-infrastructure.md` — internalize DAP message schema and IPC bridge design
- [ ] 1.4 Review DAP 1.65 spec: `initialize`, `attach`, `setBreakpoints`, `configurationDone`, `stackTrace`, `scopes`, `variables`, `continue`, `next`, `stepIn`, `stepOut`, `pause`, `disconnect` requests

## 2. Engine — Lua Debug Hooks

- [ ] 2.1 Create `src/engine/scripting/lua_debug_hooks.h/cpp`
- [ ] 2.2 Implement `LuaDebugHooks::enable(lua_State*)` — calls `lua_sethook(L, hook_fn, LUA_MASKLINE|LUA_MASKCALL|LUA_MASKRET, 0)`
- [ ] 2.3 Implement `LuaDebugHooks::disable(lua_State*)` — calls `lua_sethook(L, nullptr, 0, 0)`
- [ ] 2.4 Implement `hook_fn(lua_State*, lua_Debug*)` — line event: check breakpoint table; pause if hit
- [ ] 2.5 Implement step mode handling inside hook: `STEP_OVER` (pause at same depth), `STEP_INTO` (any next line), `STEP_OUT` (pause when depth decreases)
- [ ] 2.6 Implement `LuaDebugHooks::get_stack_trace(lua_State*, int max_frames) -> vector<StackFrame>`:
  - Iterate `lua_getstack` levels
  - Call `lua_getinfo(L, "nSl", &ar)` for name, source, currentline
  - Collect `{name, source, line, level}`
- [ ] 2.7 Implement `LuaDebugHooks::get_locals(lua_State*, int frame_level) -> vector<Variable>`:
  - `lua_getlocal` iteration until returns nullptr
  - Convert value to string representation (nil/bool/number/string/table addr/userdata)
- [ ] 2.8 Implement `LuaDebugHooks::get_upvalues(lua_State*, int frame_level) -> vector<Variable>`

## 3. Engine — Lua Debug Session

- [ ] 3.1 Create `src/engine/scripting/lua_debug_session.h/cpp`
- [ ] 3.2 Implement `LuaDebugSession` — owns `LuaDebugHooks`, breakpoint table, pause semaphore
- [ ] 3.3 Implement `set_breakpoints(path, vector<int> lines)` — store in `unordered_map<string, set<int>>`
- [ ] 3.4 Implement `pause()` / `resume()` — semaphore signal/wait (game thread blocks at hook until resumed)
- [ ] 3.5 Implement `step_over()` / `step_into()` / `step_out()` — set step mode, call resume
- [ ] 3.6 Implement `is_breakpoint_at(path, line) -> bool` — called from hook_fn
- [ ] 3.7 Ensure `LuaDebugSession` is created only in `#ifdef UZ_EDITOR` builds

## 4. IPC Handler — debug.dap

- [ ] 4.1 Create `src/runtime/ipc/handlers/dap_handlers.cpp`
- [ ] 4.2 Register `debug.dap_attach` handler — enable hooks, create session, send `Initialized` event
- [ ] 4.3 Register `debug.dap_detach` handler — disable hooks, destroy session
- [ ] 4.4 Register `debug.set_breakpoints` handler — `{ path, lines[] }` → `session.set_breakpoints()`
- [ ] 4.5 Register `debug.continue` handler — `session.resume()`, send `Continued` event
- [ ] 4.6 Register `debug.next` handler — `session.step_over()`
- [ ] 4.7 Register `debug.step_in` handler — `session.step_into()`
- [ ] 4.8 Register `debug.step_out` handler — `session.step_out()`
- [ ] 4.9 Register `debug.stack_trace` handler — `{ thread_id }` → `session.get_stack_trace()` → JSON frames array
- [ ] 4.10 Register `debug.scopes` handler — `{ frame_id }` → return locals and upvalue scope descriptors
- [ ] 4.11 Register `debug.variables` handler — `{ scope_id }` → `session.get_locals/get_upvalues()` → JSON array
- [ ] 4.12 Push `debug.breakpoint_hit` event when hook fires: `{ source, line, reason:"breakpoint", thread_id:1 }`
- [ ] 4.13 Register all handlers under `#ifdef UZ_EDITOR` in `IpcRegistry::setup()`

## 5. VSCode Extension — LuaDebugAdapter

- [ ] 5.1 Create `vscode-extension/src/lua-debug-adapter.ts`
- [ ] 5.2 Implement `UzEngineLuaDebugAdapter extends DebugSession` (VS Code DAP SDK)
- [ ] 5.3 Implement `attachRequest` — send `debug.dap_attach` IPC, await `Initialized`
- [ ] 5.4 Implement `setBreakPointsRequest` — send `debug.set_breakpoints` IPC with resolved paths
- [ ] 5.5 Implement `configurationDoneRequest` — send `debug.continue` to unpause engine
- [ ] 5.6 Implement `stackTraceRequest` — send `debug.stack_trace` IPC, map response to DAP `StackFrame[]`
- [ ] 5.7 Implement `scopesRequest` — return fixed scopes (Locals, Upvalues) with frame_id encoding
- [ ] 5.8 Implement `variablesRequest` — send `debug.variables` IPC, map to DAP `Variable[]`
- [ ] 5.9 Implement `continueRequest`, `nextRequest`, `stepInRequest`, `stepOutRequest` — forward to IPC
- [ ] 5.10 Handle incoming `debug.breakpoint_hit` event — fire DAP `StoppedEvent` to VS Code
- [ ] 5.11 Register debug adapter in `package.json` — `type: "uzengine"`, `request: "attach"`
- [ ] 5.12 Add `.vscode/launch.json` template to project scaffold

## 6. Testing

- [ ] 6.1 Unit test: `is_breakpoint_at("res://game/npc.lua", 42)` returns true after `set_breakpoints`
- [ ] 6.2 Unit test: `get_stack_trace` returns correct frame depth and source paths
- [ ] 6.3 Unit test: `get_locals` returns correct variable names and string-formatted values
- [ ] 6.4 Unit test: step_over pauses at next line in same function, not inside called function
- [ ] 6.5 Integration test: attach DAP, set breakpoint at line 5 of `npc.lua`, run — verify `breakpoint_hit` event fires
- [ ] 6.6 Integration test: step_into enters called function; step_out resumes after return
- [ ] 6.7 Integration test: detach DAP — hooks disabled, game runs at full speed

## 7. Documentation

- [ ] 7.1 Update `docs/specs/debug-infrastructure.md` — mark DAP bridge section as implemented
- [ ] 7.2 Update `docs/guides/lua-scripting-guide.md` — "Debugging with DAP" section with attach steps
- [ ] 7.3 Update `docs/modules/runtime/README.md` — document `dap_handlers.cpp`
