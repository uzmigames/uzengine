# Proposal: phase2-cvar-developer-config-system

## Why
Engine parameters such as shadow cascade distance, LOD bias, physics substep count, and audio bus volumes currently require a code change and full recompile to adjust. This creates an extremely slow iteration loop for both engineers tuning rendering and designers balancing physics. Professional engines solve this with a Console Variable (CVar) system where any registered value can be read or written at runtime via the developer console, `.uzproj` initial values, or Lua scripts — no recompilation required.

## What Changes
- Implement `CVar<T>` template in `src/core/cvar.h` supporting `bool`, `int32_t`, `float`, and `std::string` specialisations, backed by a type-erased `CVarEntry` that stores name, description, flags, default value, and current value
- Implement `CVarRegistry` singleton with `Register<T>`, `Get<T>`, `Set<T>`, and `FindByName` methods; registration uses a static initialiser pattern so CVars self-register at startup without explicit init order concerns
- Integrate with the existing `ConsoleCommandRegistry` (`src/console/`) so typing `r.shadow_distance 50` in the `~` console sets the CVar and typing `r.shadow_distance` alone prints its current value and description
- Load CVar initial values from the `[cvars]` section of `.uzproj` at engine startup, before subsystem init, so subsystems read their configured defaults
- Expose `ctx.cvar:get(name)` and `ctx.cvar:set(name, value)` in the Lua scripting API (`src/scripting/lua_cvar.cpp`)
- Add IPC handler `engine.cvar_set` and `engine.cvar_get` so the VSCode extension can read/write CVars from the editor

## Impact
- Affected specs: `docs/specs/console-commands.md`, `docs/specs/lua-scripting.md`, `docs/specs/renderer.md`, `docs/specs/physics-system.md`
- Affected code: `src/core/cvar.h`, `src/core/cvar.cpp`, `src/console/console_command_registry.cpp`, `src/scripting/lua_cvar.cpp`, `src/ipc/ipc_handlers.cpp`
- Breaking change: NO
- User benefit: Engineers and designers can tune any CVar at runtime via the console or Lua without recompiling; `.uzproj` project files can ship with tuned default CVar values per project
