# Module: Runtime

> **Location**: `src/runtime/`
> **Output**: `UzEngine.exe`
> **Language**: C++23
> **Role**: Entry point. Wires all modules together. Owns all threads.

---

## Responsibility

The Runtime module is the **executable**. It creates the process, boots subsystems in the correct order, creates and synchronizes all threads, and shuts down cleanly. It contains no game logic, no rendering detail, and no UI layout — it delegates all of that to Engine, Renderer, UI, and `game.dll`.

---

## Operating Modes

Selected via CLI flag at launch. The VSCode extension spawns Runtime with `--editor`; the shipped game runs with `--play`.

```
UzEngine.exe --editor [--project <path>] [--scene <path>] [--port <n>]
UzEngine.exe --play   [--project <path>] [--scene <path>]
```

| Aspect | `--editor` mode | `--play` mode |
|--------|----------------|---------------|
| Window | Hidden (headless HWND for DX12) | Fullscreen or windowed |
| IPC server | Active (command.json polling) | Stripped (`UZ_SHIPPING=1`) |
| WebSocket | Active (JPEG stream port 9002) | Stripped |
| JPEG readback | Active (streaming) | Stripped |
| DXC (shader compiler) | Active (hot-reload) | Pre-compiled shaders from `.uzpak` |
| Asset importers | Active | Stripped — reads `.uzmesh`/`.dds` from `.uzpak` |
| `game.dll` | Hot-reloadable via shadow copy | Statically linked or baked into `.uzpak` |
| File watcher | Active | Stripped |

---

## Source Structure

```
src/runtime/
├── main.cpp              — WinMain, mode detection (--editor / --play), boot sequence
├── game_host.h/cpp       — GameContext construction, game.dll C ABI loader
├── hot_reload.h/cpp      — Shadow copy, save_state/load_state protocol, build trigger
├── thread_config.h       — Thread priorities, affinities, names (SetThreadDescription)
└── ipc/
    ├── ipc_registry.h/cpp    — Handler dispatch table (read-only after setup)
    ├── ipc_poller.h/cpp      — command.json polling thread (50 ms)
    └── handlers/
        ├── engine_handlers.cpp   — engine.ping, engine.status, engine.screenshot
        ├── scene_handlers.cpp    — scene.addNode, scene.setProperty, scene.getTree, ...
        ├── shader_handlers.cpp   — shader.reload, shader.getErrors, shader.list
        ├── asset_handlers.cpp    — asset.import, asset.list
        └── editor_handlers.cpp   — editor.selectNode, editor.getSceneTree (UZ_EDITOR only)
```

---

## Boot Sequence

See [lifecycle-and-concurrency.md](../../engine/lifecycle-and-concurrency.md) for the full 18-step sequence. Summary:

```
1.  Platform init (timer resolution, Winsock, DPI)
2.  Logger
3.  Permanent arena (128 MB)
4.  ResourceManager
5.  WsServer::start()            [--editor only]
6.  Encode thread born
7.  DX12Renderer::init()
8.  ShaderCompiler::init()       [--editor only]
9.  SceneManager::load()
10. IpcRegistry::setup()         [--editor only]
11. IpcPoller::start()           [--editor only]
12. FileWatcher::start()         [--editor only]
13. HotReloadManager::init()     [--editor only]
     └─ game_api.on_start(ctx)
14. Game Logic thread born       [Phase 3]
15. → Render Loop
```

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
    uint32_t api_version;          // must be UZ_GAME_API_VERSION
    Scene*   scene;
    entt::registry* world;
    const InputState* input;
    float    delta_time;
    double   time_elapsed;
    uint64_t frame_index;
    void   (*log_fn)(const char*);
    ResourceHandle<Mesh>     (*load_mesh)(const char*);
    ResourceHandle<Material> (*load_mat)(const char*);
    UiDocument* (*load_ui)(const char*);   // UI module hook
    uint8_t  _reserved[128];               // always zero, for future fields
};

typedef struct {
    void (*on_start)(GameContext*);
    void (*on_update)(GameContext*, float dt);
    void (*on_stop)(GameContext*);
    void (*on_save_state)(GameContext*, Arena* out);
    void (*on_load_state)(GameContext*, void* blob, size_t size);
} game_api_t;
```

`HotReloadManager` calls `on_save_state` before `FreeLibrary`, then `on_load_state` after the new shadow copy is loaded. State survives reload.

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
- [hot-reload.md](../../engine/hot-reload.md) — shadow copy DLL protocol
- [../engine/README.md](../engine/README.md) — subsystems initialized by Runtime
- [../renderer/README.md](../renderer/README.md) — Renderer initialized and driven by Runtime
