# ECS Systems Engineer — Persistent Memory

## Project: UzEngine
C++23 game engine. ECS via EnTT. game.dll C ABI boundary. LuaJIT scripting.

## Key Paths
- `src/engine/ecs/components.h` — all ECS component structs
- `src/engine/scripting/` — lua_binding, script_runner, lua_sandbox (no LuaJIT linked)
- `src/game/` — game.dll implementation (owns LuaJIT, physics, timers)
- `docs/specs/` — authoritative specs for all systems

## Architecture Rules
- game.dll C ABI: never cross boundary with C++ types (vtables, STL, RTTI)
- GameContext._reserved[128] must always be zeroed
- LuaJIT linked ONLY in game.dll — never uz_engine.lib or UzEngine.exe
- Timer drain happens BEFORE node script on_update each frame
- EventBus events dispatched on Game Logic thread only

## Component Naming
- Prefix: `C` (e.g., CTransform, CMeshRenderer, CScriptRef)
- Plain data structs only — no virtuals, no side-effect constructors

## .uzscene Format
- INI-like, res:// paths, id=0 is null/invalid, parent=0 = scene root
- Script extension determines kind: .lua → SCRIPT_LUA, .cpp → SCRIPT_CPP
