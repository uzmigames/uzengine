## 1. Engine — LuaJIT Integration

- [x] 1.1 Add LuaJIT 2.1 to vcpkg manifest (`vcpkg.json`) — static lib, x64-windows
- [x] 1.2 Create `src/engine/scripting/lua_sandbox.h/cpp` — strip dangerous globals from `lua_State`
- [x] 1.3 Create `src/engine/scripting/lua_binding.h/cpp` — register Vec3, Quat, NodeRef, Scene, InputState metatables
- [x] 1.4 Create `src/engine/scripting/script_runner.h/cpp` — `lua_script_load`, `lua_script_on_start`, `lua_script_on_update`, `lua_script_on_stop`, `lua_script_release`
- [x] 1.5 Update `ecs/components.h` — add `ScriptKind` enum, extend `CScriptRef` with `kind` field
- [x] 1.6 Update `CMakeLists.txt` — link LuaJIT into `game` target only (not `uz_engine`)

## 2. Runtime — LuaVM Lifecycle in game.dll

- [x] 2.1 Create `src/game/lua_vm.h/cpp` — VM create/destroy, scene-scoped lifetime, module registry
- [x] 2.2 Update `uz_game_start` — init VM, load all `CScriptRef` Lua nodes, call `on_start`
- [x] 2.3 Update `uz_game_update` — dispatch `on_update` for all active Lua nodes
- [x] 2.4 Update `uz_game_stop` — call `on_stop`, close VM
- [x] 2.5 Update `uz_game_save_state` / `uz_game_load_state` — serialize/restore Lua instance tables

## 3. Hot-Reload

- [x] 3.1 Create `src/runtime/lua_script_reloader.h/cpp` — watches `.lua` files, dispatches reload to `LuaVM`
- [x] 3.2 Register `.lua` extension in `FileWatcher` dispatch table
- [x] 3.3 `LuaScriptReloader::reload(path)` — call `on_stop`, reload module, call `on_start` on affected nodes

## 4. IPC Handlers (Editor Only)

- [x] 4.1 Create `src/runtime/ipc/handlers/lua_handlers.cpp` — `lua.eval`, `lua.reload_script`, `lua.get_errors`, `lua.get_globals`
- [x] 4.2 Register handlers in `IpcRegistry::setup()` under `#ifdef UZ_EDITOR`

## 5. VSCode Extension — Lua Diagnostics

- [x] 5.1 Add `.lua` error parsing to `shader-diagnostics.ts` (reuse existing pattern)
- [x] 5.2 Map `lua.get_errors` IPC response to VSCode `DiagnosticCollection`

## 6. Testing

- [x] 6.1 Unit tests: `lua_sandbox` — verify forbidden globals are nil
- [x] 6.2 Unit tests: `lua_binding` — Vec3 arithmetic, NodeRef read/write, InputState access
- [x] 6.3 Unit tests: `script_runner` — on_start/on_update/on_stop dispatch, error isolation
- [x] 6.4 Integration test: attach `.lua` script in `.uzscene`, verify `on_update` runs
- [x] 6.5 Integration test: modify `.lua` file, verify hot-reload completes in < 200ms
- [x] 6.6 Integration test: Lua runtime error causes node disable, engine continues

## 7. Documentation (planning phase — all done)

- [x] 7.1 `docs/specs/lua-scripting.md` — full spec
- [x] 7.2 `docs/engine/design-decisions.md` — section 6 Lua rationale
- [x] 7.3 `docs/modules/engine/README.md` — scripting/ subsystem
- [x] 7.4 `docs/engine/hot-reload.md` — Lua reload section
- [x] 7.5 `docs/engine/cpp-architecture.md` — CScriptRef update, LuaBinding API
- [x] 7.6 `docs/README.md` — lua-scripting.md in specs table
