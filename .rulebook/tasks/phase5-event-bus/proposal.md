# Proposal: phase3-event-bus

## Why

Game systems in Phase 3+ (physics callbacks, animation notifies, input actions, ragdoll state changes) need to communicate without direct coupling. Without an EventBus, every cross-system communication requires a direct reference or a manual callback registration, producing spaghetti dependencies that break hot-reload and make AI-generated game logic difficult to compose. A typed C++ `EventBus<T>` with a Lua string-dispatch mirror gives both layers a clean decoupled publish-subscribe mechanism. This is the standard pattern used in every major game engine (Godot Signals, Unreal delegates, Unity events) and is the minimal infrastructure needed before physics and animation callbacks can be exposed to Lua scripts.

## What Changes

- **`src/engine/event_bus.h`** — New header: `EventBus<T>` template, `AutoSub<T>` RAII wrapper, `SubId` type, predefined engine event structs (`SceneLoaded`, `SceneUnloaded`, `NodeAdded`, `NodeRemoved`)
- **`src/engine/event_bus.cpp`** — Template explicit instantiations for predefined event types
- **`src/game/lua_vm.h/cpp`** — Add `lua_subscribers_` map, `LuaSubEntry` struct, Lua dispatch method, per-instance sub tracking for auto-cleanup
- **`src/engine/scripting/lua_binding.h/cpp`** — Register `ctx.events` userdata with `emit`, `subscribe`, `unsubscribe` methods; wire C++ engine event bridges (SceneLoaded etc. → Lua dispatch)
- **`src/runtime/ipc/handlers/scene_handlers.cpp`** — Add `events.list_subscribers` IPC handler (editor-only)
- **`docs/specs/event-bus.md`** — Full spec (already written in planning phase)
- **No breaking changes to existing IPC or scene API** — purely additive

Execution model: all emit/subscribe calls are restricted to the Game Logic thread. No lock needed — single-threaded dispatch. Re-entrant emit is asserted in debug builds.

## Impact

- Affected specs: `docs/specs/event-bus.md` (new), `docs/specs/lua-scripting.md` (add ctx.events section), `docs/specs/scene-system.md` (predefined events table)
- Affected modules: Engine (`event_bus.h/cpp`), Engine scripting (`lua_binding`), game.dll (`lua_vm`), Runtime IPC (`scene_handlers`)
- Breaking change: NO — additive only; no existing API modified
- User benefit: Lua scripts can react to engine events (`SceneLoaded`, `NodeAdded`) without polling; C++ game systems decouple via typed events; AI agents can generate event-driven Lua behaviors without understanding system internals; `events.list_subscribers` IPC enables editor debugging of signal graph
