# Module: Game

**Location:** `src/game/`
**Output:** `game.dll` (hot-reloadable shared library)
**Language:** C++23
**Role:** All game-specific logic. Owns LuaJIT VM, physics simulation, timer management.

---

## Responsibility

The Game module encapsulates all game-specific functionality in a separate DLL that can be hot-reloaded without restarting the entire engine. This separation provides three critical capabilities:

1. **Hot-reload stability**: Save game state → unload game.dll → recompile → reload game.dll → restore state (~300ms)
2. **Scripting isolation**: LuaJIT VM lives here exclusively; never linked into uz_engine.lib or render.exe
3. **Physics ownership**: Jolt Physics simulation state tied to game logic; resets cleanly with game reload

The game.dll exposes a minimal C ABI contract (`game_api_t` function pointers) to the engine. All outbound communication flows via callbacks in `GameContext`, preserving the stable interface across reloads.

---

## C ABI Contract

All game.dll exports follow a strict C interface to ensure stability across DLL boundaries:

```c
// game_api.h (exported interface)
typedef struct {
    void (*on_start)(GameContext*);
    void (*on_update)(GameContext*, float dt);
    void (*on_stop)(GameContext*);
    void (*on_save_state)(GameContext*, SaveWriter*);
    void (*on_load_state)(GameContext*, SaveReader*);
    int api_version;  // Checked at load time
} game_api_t;

// At DLL load:
game_api_t* api = (game_api_t*)GetProcAddress(hGameDll, "game_api");
if (api->api_version != GAME_API_VERSION) {
    FreeLibrary(hGameDll);
    return error("API version mismatch");
}
```

The `GameContext` struct passed to each callback contains pointers to engine services (audio, physics queries, scene, input, events, saves, locale, console). This allows game.dll to drive behavior without holding engine references.

---

## Source Structure

```
src/game/
├── game.cpp                    — on_start / on_update / on_stop / on_save_state / on_load_state entry points
├── game.h                      — GameAPI contract, GameContext definition
├── lua_vm.h/cpp                — LuaVM lifecycle: create, destroy, load_script, drain_timers
├── lua_script_reloader.h/cpp   — FileWatcher subscriber; on_stop/reload/on_start hot-reload cycle
├── systems/
│   ├── physics_step.h/cpp      — Fixed 60Hz Jolt update, transform sync, collision event drain
│   ├── audio_step.h/cpp        — Per-frame CAudioSource spatial update via X3DAudio
│   ├── animation_step.h/cpp    — AnimClip sampler, state machine transitions
│   └── nav_step.h/cpp          — DetourCrowd steering update
└── physics/
    ├── physics_world.h/cpp     — JPH::PhysicsSystem wrapper, TempAllocator
    ├── physics_body_sync.h/cpp — CRigidBody ↔ JPH::Body transform synchronization
    ├── physics_queries.h/cpp   — raycast, linecast, overlap_sphere, overlap_box
    ├── contact_listener.h/cpp  — JPH::ContactListener → thread-safe collision queue
    ├── character_controller.h/cpp — CCharacterController → JPH::CharacterVirtual
    └── physics_debug_draw.h/cpp— JPH::DebugRenderer → IPC line primitives (#ifdef UZ_EDITOR)
```

---

## on_update Execution Order

The game loop processes updates in a strict sequence to ensure consistent physics, collision, scripting, and audio state:

```
1. NavMesh crowd update (DetourCrowd::update)
   └─ Crowd agents resolve local avoidance steering

2. Physics step (fixed 60Hz accumulator)
   └─ Jolt::PhysicsSystem::Update(PHYSICS_DT)
   └─ Sync all CRigidBody transforms ← Jolt body positions

3. Drain collision events (EventBus emit from queue)
   └─ ContactListener thread-safe queue → EventBus<OnCollision>
   └─ Triggers Lua on_collision callbacks

4. Drain Lua timer queue (drain_timers(dt))
   └─ Decrement active timers, fire schedule_after/schedule_every callbacks

5. Script on_update (all active CScriptRef nodes)
   └─ lua_pcall(on_update_func, node_context, dt)
   └─ Node disabled if Lua errors

6. Audio spatial update (X3DAudio DSP per CAudioSource[spatial])
   └─ Update listener position, compute spatial DSP

7. Animation step (clip sampler + state machine)
   └─ Sample animation frames, trigger AnimNotify events
```

This ordering ensures:
- Physics determinism (fixed dt, consistent accumulator)
- Lua scripts see current physics state
- Timers fire before user scripts
- Audio 3D position updates happen after all motion
- Animation frames drive skeletal mesh correctly

---

## Save/Load Protocol

Save/load maintains game state across DLL reloads using a msgpack-based serialization format:

```cpp
// on_save_state called before DLL unload
void on_save_state(GameContext* ctx, SaveWriter* writer) {
    // Serialize all active Lua instances
    lua_vm->serialize_all_instances(writer);

    // Serialize physics state
    physics_world->save_state(writer);

    // Serialize game-specific values
    writer->write_value("player_health", player_health);
    writer->write_value("current_level", current_level);
}

// on_load_state called after DLL reload
void on_load_state(GameContext* ctx, SaveReader* reader) {
    // Restore Lua instances
    lua_vm->deserialize_all_instances(reader);

    // Restore physics state
    physics_world->load_state(reader);

    // Restore game values
    player_health = reader->read_value("player_health", 100);
    current_level = reader->read_value("current_level", 0);
}
```

The format supports arbitrary nested structures, allowing Lua tables and C++ game objects to coexist in a single save blob.

---

## Hot-Reload Protocol

The hot-reload sequence preserves all runtime state:

```
1. GameContext->save_state_fn() — game.dll on_save_state()
2. FreeLibrary(hGameDll)         — Unload game.dll
3. [Rebuild game.dll]
4. LoadLibrary(hGameDll)         — Reload game.dll
5. Check api->api_version        — Abort if mismatch
6. game.dll on_start()           — Reinitialize LuaVM, physics
7. GameContext->load_state_fn()  — Restore serialized state
8. on_update() resumes normally
```

Target reload time: **~300ms** (save + unload + load + restore).

---

## Key Invariants

1. **LuaJIT links ONLY here** — game.dll is the sole consumer of luajit.lib. uz_engine.lib and render.exe never link LuaJIT.

2. **Jolt Physics links ONLY here** — physics state and lifetime tied to game logic; clean reset on reload.

3. **game.dll NEVER owns GPU resources** — all GPU objects (textures, meshes, materials, shaders) live in Renderer module. game.dll only holds ResourceHandle<T> (24-bit index).

4. **All outward communication via GameContext callbacks** — game.dll never calls engine functions directly. All engine services accessed through `GameContext*` parameter.

5. **C ABI only across DLL boundary** — no C++ RTTI, exceptions, STL objects, or virtual calls passed to/from game.dll. Use serialization (msgpack) for complex data.

6. **No circular dependencies** — game.dll depends on uz_engine.lib, but uz_engine.lib never includes game.h or depends on game.dll at link time.

---

## Related Docs

- [Physics Module](../physics/README.md) — Physics system design and queries
- [Scripting Module](../scripting/README.md) — LuaJIT binding and sandbox
- [Engine Architecture](../../engine/hot-reload.md) — Game DLL hot-reload mechanism
- [Physics System Spec](../../specs/physics-system.md) — Jolt Physics API and collision
- [Lua Scripting Spec](../../specs/lua-scripting.md) — LuaJIT per-node scripting
- [Event Bus Spec](../../specs/event-bus.md) — C++ and Lua event dispatch
- [Timer System Spec](../../specs/timer-system.md) — Lua timer scheduling
