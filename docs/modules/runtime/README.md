# Module: Runtime

> **Location**: `src/runtime/`
> **Output**: `render.exe`
> **Language**: C++23
> **Role**: Entry point. Wires all modules together. Owns all threads.

---

## Responsibility

The Runtime module is the **executable**. It creates the process, boots subsystems in the correct order, creates and synchronizes all threads, and shuts down cleanly. It contains no game logic, no rendering detail, and no UI layout — it delegates all of that to Engine, Renderer, UI, and `game.dll`.

---

## Operating Modes

Selected via CLI flag at launch. The VSCode extension spawns Runtime with `--editor`; the shipped game runs with `--play`.

```
render.exe --editor [--project <path>] [--scene <path>] [--port <n>]
render.exe --play   [--project <path>] [--scene <path>]
```

| Aspect | `--editor` mode | `--play` mode |
|--------|----------------|---------------|
| Window | Hidden (headless HWND for DX12) | Fullscreen or windowed |
| IPC server | Active (command.json polling) | Stripped (`UZ_SHIPPING=1`) |
| WebSocket | Active (JPEG stream port 9002) | Stripped |
| JPEG readback | Active (streaming) | Stripped |
| DXC (shader compiler) | Active (hot-reload, ~60ms reload) | Pre-compiled shaders from `.uzpak` |
| Asset importers | Active (reimport on change) | Stripped — reads `.uzmesh`/`.dds`/`.uzaudio` from `.uzpak` |
| `game.dll` | Hot-reloadable via shadow copy (~2500ms reload) | Statically linked or baked into `.uzpak` |
| Lua script hot-reload | Active (~60ms per changed script) | Disabled — scripts are bytecode-compiled in `.uzpak` |
| File watcher | Active (shaders, scenes, Lua scripts) | Stripped |

---

## Source Structure

```
src/runtime/
├── main.cpp              — WinMain, mode detection (--editor / --play), boot sequence, thread creation
├── game_host.h/cpp       — GameContext construction, game.dll C ABI loader, callback dispatch
├── hot_reload.h/cpp      — Shadow copy DLL, save_state/load_state protocol, rebuild trigger
├── lua_script_reloader.h/cpp  — Lua script hot-reload coordinator, file watcher integration
├── thread_config.h       — Thread priorities, affinities, names (SetThreadDescription)
└── ipc/
    ├── ipc_registry.h/cpp    — Handler dispatch table (read-only after setup)
    ├── ipc_poller.h/cpp      — command.json polling thread (50 ms)
    └── handlers/
        ├── engine_handlers.cpp      — engine.ping, engine.status, engine.screenshot
        ├── scene_handlers.cpp       — scene.addNode, scene.setProperty, scene.getTree, scene.serialize, events.list_subscribers
        ├── shader_handlers.cpp      — shader.reload, shader.getErrors, shader.list, shader.getVariants
        ├── asset_handlers.cpp       — asset.import, asset.list, asset.reimport
        ├── audio_handlers.cpp       — audio.play, audio.stop_all, audio.set_bus_volume (Phase 2)
        ├── physics_handlers.cpp     — physics.raycast, physics.setDebugPhysics, physics.getStats (Phase 3)
        ├── lua_handlers.cpp         — lua.execute, lua.getGlobals, lua.setBreakpoint (Phase 1)
        ├── console_handlers.cpp     — console.execute, console.list_commands (Phase 1)
        └── editor_handlers.cpp      — editor.selectNode, editor.getSceneTree, editor.undo, editor.redo (UZ_EDITOR only)
```

---

## Boot Sequence

See [lifecycle-and-concurrency.md](../../engine/lifecycle-and-concurrency.md) for the full sequence. Key points:

```
1.  Platform init (timer resolution, Winsock, DPI)
2.  Logger + crash reporting
3.  Permanent arena (128 MB)
4.  ResourceManager + asset import registry
5.  D3D12 Device creation
6.  WsServer::start() on port 9002              [--editor only]
7.  Encode thread born (JPEG streaming)
8.  DX12Renderer::init()
9.  ShaderCompiler::init()                      [--editor only]
10. SceneManager::load() + prefab expansion
11. IpcRegistry::setup()                        [--editor only]
12. IpcPoller::start() (50 ms poll loop)        [--editor only]
13. FileWatcher::start() (shader, scene files)  [--editor only]
14. HotReloadManager::init()                    [--editor only]
15. Load game.dll, call game_api.on_start(ctx)
     ├─ game.dll creates LuaVM
     ├─ lua_binding registers metatables (Vec3, Quat, ctx.* APIs)
     ├─ lua_binding registers ctx.schedule_after, ctx.events.subscribe, etc. (Phase 1+)
     └─ game.dll calls lua_vm.load_scripts() or calls on_start from main.lua
16. LuaScriptReloader::init()                   [--editor only, hot-reload watcher]
17. Game Logic thread born
18. → Main render loop (20 ms frames)
```

**Lua VM initialization** happens in `game.dll` after `game_api.on_start()`. The VM owns `LuaTimer`, script instances, and Lua event bus state. Hot-reload in editor mode calls `lua_script_reloader.on_file_change()` to re-parse changed `.lua` files and re-attach scripts without losing Lua table state (via `on_stop` / `on_start` hooks).

---

## Thread Ownership

| Thread | Born in | Joined in |
|--------|---------|-----------|
| Encode | `main.cpp` after WsServer starts | Shutdown step 6 |
| IPC Poller | `IpcPoller::start()` | Shutdown step 5 |
| File Watcher | `FileWatcher::start()` | Shutdown step 4 |
| Game Logic | `main.cpp` after `game_api.on_start` | Shutdown step 1 |

All thread lifecycle is owned by `main.cpp`. No thread is created inside Engine or Renderer.

---

## `game.dll` Contract

```cpp
// core/game_api.h — stable C ABI, never change field order
struct GameContext {
    // Core
    uint32_t api_version;          // must be UZ_GAME_API_VERSION
    Scene*   scene;
    entt::registry* world;
    const InputState* input;
    float    delta_time;
    double   time_elapsed;
    uint64_t frame_index;

    // Logging
    void   (*log_fn)(const char*);

    // Resource loading
    ResourceHandle<Mesh>     (*load_mesh)(const char*);
    ResourceHandle<Material> (*load_mat)(const char*);
    ResourceHandle<AudioClip> (*load_audio)(const char*);  // Phase 2

    // UI module hook
    UiDocument* (*load_ui)(const char*);

    // Audio playback callbacks (Phase 2)
    void (*audio_play_2d)(const char* clip_path, float volume);
    void (*audio_play_3d)(const char* clip_path, Vec3 position, float volume, float max_distance);
    void (*audio_stop)(uint32_t voice_id);
    void (*audio_set_bus_volume)(const char* bus_name, float volume);

    // Physics debug callbacks (Phase 3)
    void (*physics_set_debug_draw)(bool enabled);
    void (*physics_raycast)(Vec3 origin, Vec3 direction, float max_dist, uint32_t layer_mask, void (*callback)(RaycastHit));

    // Console callbacks (Phase 1)
    void (*console_execute)(const char* command);

    uint8_t  _reserved[128];  // always zero, for future fields
};

typedef struct {
    void (*on_start)(GameContext*);
    void (*on_update)(GameContext*, float dt);
    void (*on_stop)(GameContext*);
    void (*on_save_state)(GameContext*, Arena* out);
    void (*on_load_state)(GameContext*, void* blob, size_t size);
} game_api_t;
```

**Hot-reload protocol**: `HotReloadManager` calls `on_save_state` before `FreeLibrary`, serializing game state to an Arena buffer. After the new shadow copy loads, it calls `on_load_state` with the buffer, restoring state. Timers, events, and Lua table state are re-initialized via script `on_start` hooks. No player progress is lost.

---

## Shutdown Sequence

Strict reverse of boot. Key points:
- Game thread is stopped first (drains last `on_update`)
- Encode thread is joined before WsServer (drains ring buffer)
- `WaitForGpu()` before releasing DX12 objects
- `timeEndPeriod(1)` always called even on crash path (Win32 requirement)

---

## Key Invariants

1. **All threads are born and joined in `main.cpp`.** No other module creates threads.
2. **`GameContext._reserved` is always zeroed.** Game code may `memcmp` version fields.
3. **`--play` mode never touches IPC, FileWatcher, or WsServer code paths** — guarded by `#ifdef UZ_EDITOR`.
4. **`HotReloadManager` only calls `FreeLibrary` after `on_save_state` completes.** State is never lost on reload.

---

## Related Docs

- [lifecycle-and-concurrency.md](../../engine/lifecycle-and-concurrency.md) — full boot/shutdown + concurrency model
- [hot-reload.md](../../engine/hot-reload.md) — shadow copy DLL protocol, state persistence
- [../engine/README.md](../engine/README.md) — subsystems initialized by Runtime
- [../renderer/README.md](../renderer/README.md) — Renderer initialized and driven by Runtime
- [../../specs/lua-scripting.md](../../specs/lua-scripting.md) — Lua VM lifecycle, hot-reload coordinator
- [../../specs/audio-system.md](../../specs/audio-system.md) — Audio callbacks in GameContext
- [../../specs/physics-system.md](../../specs/physics-system.md) — Physics query callbacks
- [../../specs/console-commands.md](../../specs/console-commands.md) — Console command registry

## Rulebook Tasks

Core infrastructure: [phase1-lua-scripting](../../.rulebook/tasks/phase1-lua-scripting/), [phase1-console-commands](../../.rulebook/tasks/phase1-console-commands/), [phase1-crash-reporting](../../.rulebook/tasks/phase1-crash-reporting/), [phase1-debug-profiling](../../.rulebook/tasks/phase1-debug-profiling/)

Audio system: [phase2-audio-system](../../.rulebook/tasks/phase2-audio-system/)

Physics + events: [phase3-physics-system](../../.rulebook/tasks/phase3-physics-system/), [phase3-event-bus](../../.rulebook/tasks/phase3-event-bus/)
