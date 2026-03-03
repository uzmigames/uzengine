## 1. CVar Core Types

- [ ] 1.1 Create `src/core/cvar.h` defining the `CVarEntry` type-erased struct with fields: `name` (string_view), `description` (string_view), `flags` (uint32_t bitmask: `CVAR_READONLY`, `CVAR_CHEAT`, `CVAR_SAVED`), `type` (enum: BOOL, INT, FLOAT, STRING), `default_value` (variant), and `current_value` (variant)
- [ ] 1.2 Implement `CVar<bool>`, `CVar<int32_t>`, `CVar<float>`, and `CVar<std::string>` template specialisations in `src/core/cvar.h`, each storing a pointer to their `CVarEntry` in the registry and providing `get()` / `set()` methods with type safety
- [ ] 1.3 Add static initialiser pattern: `CVar<T>` constructors self-register with `CVarRegistry::get()` before `main()` via a global instance, ensuring all CVars are registered before any subsystem reads them at startup

## 2. CVarRegistry Singleton

- [ ] 2.1 Create `src/core/cvar_registry.h` and `src/core/cvar_registry.cpp` implementing `CVarRegistry` singleton with methods: `Register<T>(name, description, default_value, flags)`, `Get<T>(name)`, `Set<T>(name, value)`, `FindByName(name)`, and `GetAll()` returning `std::span<const CVarEntry*>`
- [ ] 2.2 Implement `CVarRegistry::Set<T>()` to reject writes when `CVAR_READONLY` flag is set and emit `UZ_LOG_WARN("CVar '%s' is read-only", name)` with no state mutation
- [ ] 2.3 Implement `CVarRegistry::Set<T>()` to reject writes when `CVAR_CHEAT` flag is set and cheat protection is enabled (controlled by `sv_cheats` CVar), logging the attempt and requester
- [ ] 2.4 Implement `CVarRegistry::FindByName()` using a `std::unordered_map<std::string_view, CVarEntry*>` for O(1) lookup; throw `std::out_of_range` (or return `nullptr`) for unknown names

## 3. Console Integration

- [ ] 3.1 Extend `src/console/console_command_registry.cpp` to register a generic CVar command handler: when a token matching a registered CVar name is typed with no arguments, print its current value, description, and flags; with one argument, call `CVarRegistry::Set<T>()`
- [ ] 3.2 Implement tab-completion in the console for CVar names by hooking `CVarRegistry::GetAll()` into the existing completion callback infrastructure
- [ ] 3.3 Add `cvar_list` console command that prints all registered CVars grouped by prefix (e.g. `r.*`, `phys.*`, `mem.*`) sorted alphabetically with their current values

## 4. Project File Loading

- [ ] 4.1 Add `[cvars]` section parsing in `src/engine/core/project_loader.cpp`: read each `name = value` pair and call `CVarRegistry::Set<T>()` before any subsystem `Init()` is called, so subsystems read their project-configured defaults
- [ ] 4.2 Validate CVar types on load: if `.uzproj` specifies `r.shadow_distance = "hello"` but the CVar is `CVar<float>`, emit `UZ_LOG_ERROR` with the key and skip the assignment without crashing
- [ ] 4.3 Implement `CVarRegistry::SaveModified(path)` that writes all CVars with the `CVAR_SAVED` flag and a non-default current value back to the `.uzproj` `[cvars]` section, used by the editor's Save Project action

## 5. Lua Scripting API

- [ ] 5.1 Create `src/scripting/lua_cvar.cpp` implementing `ctx.cvar:get(name)` which calls `CVarRegistry::FindByName(name)` and returns the current value as the appropriate Lua type (bool, number, or string)
- [ ] 5.2 Implement `ctx.cvar:set(name, value)` in `src/scripting/lua_cvar.cpp` that type-checks the Lua argument against the registered CVar type and calls `CVarRegistry::Set<T>()`; returns `false` plus an error string on type mismatch
- [ ] 5.3 Register the `cvar` table in the Lua VM's `ctx` global during scripting system initialization; add entries to the Lua API documentation in `docs/specs/lua-scripting.md`

## 6. IPC Handlers

- [ ] 6.1 Add `engine.cvar_get` IPC handler in `src/ipc/ipc_handlers.cpp` accepting `{"name": "r.shadow_distance"}` and returning `{"value": 50.0, "type": "float", "description": "...", "flags": 0}`
- [ ] 6.2 Add `engine.cvar_set` IPC handler in `src/ipc/ipc_handlers.cpp` accepting `{"name": "r.shadow_distance", "value": 100.0}` and returning `{"ok": true}` or `{"ok": false, "error": "CVar is read-only"}`
- [ ] 6.3 Add `engine.cvar_list` IPC handler returning a JSON array of all registered CVars with their name, type, current value, default value, and description — used by the VSCode extension CVar browser panel

## 7. Testing

- [ ] 7.1 Create `tests/core/test_cvar.cpp` with fixture that constructs an isolated `CVarRegistry` instance (not the global singleton) to avoid test order dependencies
- [ ] 7.2 Test `Register<float>("r.test", "desc", 1.0f, 0)` followed by `Get<float>("r.test")` returns `1.0f`
- [ ] 7.3 Test `Set<float>("r.test", 42.0f)` followed by `Get<float>("r.test")` returns `42.0f`
- [ ] 7.4 Test `Set<float>()` on a `CVAR_READONLY` CVar returns an error and leaves value unchanged at default
- [ ] 7.5 Test `FindByName()` with an unregistered name returns nullptr / throws as documented
- [ ] 7.6 Test the static initialiser pattern: declare a file-scope `CVar<int32_t> g_test_cvar(...)` and verify it is present in the registry before any explicit `Register()` call in test body
- [ ] 7.7 Test `CVarRegistry::GetAll()` returns all registered CVars with correct names and types
- [ ] 7.8 Test TOML project file CVar loading: provide a mock `.uzproj` string with `[cvars]` section and verify the float/bool/int values are applied correctly
- [ ] 7.9 Test TOML type mismatch: wrong-type value in `[cvars]` is skipped with an error log and the CVar retains its default value
- [ ] 7.10 Test `ctx.cvar:get()` Lua binding returns a number for a float CVar and a boolean for a bool CVar
- [ ] 7.11 Test `ctx.cvar:set()` Lua binding rejects a string value passed to a float CVar, returning `false` and an error string
- [ ] 7.12 Test `engine.cvar_get` IPC handler returns correct JSON for a registered CVar; test unknown name returns an error field
- [ ] 7.13 Test `engine.cvar_set` IPC handler sets the CVar value and verify via subsequent `engine.cvar_get`; test read-only CVar returns `ok: false`
- [ ] 7.14 Run coverage — all new source files must reach >= 95% line and branch coverage before marking complete
