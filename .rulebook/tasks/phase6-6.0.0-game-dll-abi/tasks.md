## 1. GameContext Definition

- [ ] 1.1 Create `runtime/engine/game/game_context.h` — GameContext struct (pure C, no C++ types). Fields: version (uint32), registry (void* → EnTT), frame_arena (void*), level_arena (void*). Function pointers: scene_create_entity, scene_destroy_entity, scene_get_position, scene_set_position, scene_set_rotation, scene_get_component. Audio: audio_play, audio_stop, audio_set_volume. Input: input_action_value, input_is_pressed. Time: time_delta, time_total, time_fixed_delta. Console: console_log.
- [ ] 1.2 Create `runtime/engine/game/game_context_types.h` — ABI-safe POD types: GcVec2 {float x,y}, GcVec3 {float x,y,z}, GcVec4 {float x,y,z,w}, GcQuat {float x,y,z,w}, GcMat4 {float m[16]}. EntityHandle (uint64_t). AudioHandle (uint64_t). Static asserts on sizes and alignment.
- [ ] 1.3 Create `runtime/engine/game/game_context.cpp` — PopulateGameContext(GameContext&): fills all function pointers from engine subsystems. Each function pointer wraps the C++ engine API with C linkage.

## 2. DLL Loader

- [ ] 2.1 Create `runtime/engine/game/dll_loader.h` — DllLoader: Load(path) → bool, Unload(), IsLoaded(). GetProc<T>(name) template for function pointers. Wraps LoadLibrary/FreeLibrary (Win32).
- [ ] 2.2 Create `runtime/engine/game/dll_loader.cpp` — Load: LoadLibrary, resolve 4 exports (game_dll_on_start/update/fixed_update/stop). Unload: FreeLibrary. Error logging on missing exports.
- [ ] 2.3 Create `runtime/engine/game/game_dll_host.h/.cpp` — GameDllHost: Init(dll_path, GameContext&), Shutdown(). Tick(dt): calls on_update. FixedTick(dt): calls on_fixed_update. Owns DllLoader + GameContext.

## 3. Hot-Reload Protocol

- [ ] 3.1 Add file watcher for game.dll — Watch game DLL output directory. On change: trigger reload sequence.
- [ ] 3.2 Implement reload sequence — on_stop(old) → serialize ECS state → Unload old → copy new DLL (avoid file lock) → Load new → deserialize → on_start(new). Copy to temp path to avoid build lock conflict.
- [ ] 3.3 Add state serialization for hot-reload — Serialize all component data to msgpack buffer before unload. Deserialize after reload. Use ComponentSerializerRegistry.

## 4. Sample game.dll

- [ ] 4.1 Create `game/game_main.cpp` — Minimal game.dll: implements 4 exported functions. on_start: logs "Game started". on_update: rotates an entity. on_stop: logs "Game stopped". Pure C linkage, no engine headers beyond game_context.h.
- [ ] 4.2 Create `game/build.zig` — Builds game.dll as shared library. Links only against game_context.h (header-only dependency). No engine static lib dependency.

## 5. Engine Integration

- [ ] 5.1 Wire GameDllHost into EngineLoop — If --game-dll <path> CLI flag or .uzproj has game_dll field: load game.dll at startup, call on_update/on_fixed_update in main loop, unload on shutdown.
- [ ] 5.2 Add hot-reload toggle — CVar `game.hot_reload` (bool, default true in editor mode). File watcher only active when enabled.

## 6. Testing

- [ ] 6.1 Unit tests for DllLoader — load/unload, missing export handling, version check.
- [ ] 6.2 Unit tests for GameContext — all function pointers non-null after populate.
- [ ] 6.3 Unit tests for ABI-safe types — sizeof/alignof static asserts.
- [ ] 6.4 Integration test: load sample game.dll, call on_start/on_update/on_stop, verify no crashes.
- [ ] 6.5 Integration test: simulate hot-reload cycle (load → call → unload ��� reload → call).

## 7. Documentation

- [ ] 7.1 Update `docs/engine/15-scripting/` with game.dll ABI documentation.
- [ ] 7.2 Document GameContext function pointer table with signatures and semantics.
- [ ] 7.3 Document hot-reload protocol and limitations.

## 8. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 8.1 Update or create documentation covering the implementation
- [ ] 8.2 Write tests covering the new behavior
- [ ] 8.3 Run tests and confirm they pass
