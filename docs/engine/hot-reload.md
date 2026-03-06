# UzEngine — Hot Reload System

> **Goal**: Edit C++ game logic, shaders, or scene files and see the result **in the running preview within ~1 second** — without restarting the engine process and without risk of crashing VSCode.

---

## 1. What Must Be Hot-Reloadable

| Asset Type | Reload Target | Restart Engine? | Time |
|-----------|--------------|-----------------|------|
| **HLSL/GLSL shaders** | Recompile → swap PSO | No | ~200ms |
| **Lua scripts** (`.lua`) | Reload module in VM, re-run `on_start` | No | ~60ms |
| **Scene files** (`.uzscene`) | Reload scene graph | No | ~100ms |
| **Material files** (`.uzmat`) | Reload material params | No | ~50ms |
| **Audio files** (`.wav`, `.mp3`, `.ogg`) | Re-import → invalidate handle | No | ~80ms |
| **UI files** (`.html` / `.css`) | Reload document, re-apply styles | No | ~30ms |
| **Game logic** (`.cpp` / DLL) | Unload DLL → rebuild → reload DLL | No | ~2500ms |
| **Engine C++** (renderer, ECS...) | Full rebuild + engine restart | Yes (acceptable) | ~30s |

The key insight: **game logic** lives in a separate `game.dll`. The engine is a host that loads it. Editing game code recompiles only `game.dll` (< 5s for typical projects), while the engine process (renderer, IPC, WebSocket) keeps running. VSCode never crashes because it never loads the DLL — only `render.exe` does.

---

## 2. Architecture Overview

```
┌──────────────────────────────────────────────────────────────┐
│  render.exe (host — never rebuilt during session)          │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐    │
│  │  HotReloadManager                                   │    │
│  │  ┌───────────────┐   ┌──────────────────────────┐  │    │
│  │  │ FileWatcher   │   │  DLL Loader              │  │    │
│  │  │ (Win32 RDCW)  │──▶│  LoadLibraryW            │  │    │
│  │  └───────────────┘   │  GetProcAddress           │  │    │
│  │                      │  FreeLibrary              │  │    │
│  │                      └──────────────────────────┘  │    │
│  │                            │ game_api_t vtable       │    │
│  └────────────────────────────┼─────────────────────────┘    │
│                               │                              │
│  ┌────────────────────────────▼──────────────────────────┐   │
│  │  Game API (stable ABI boundary)                       │   │
│  │  typedef struct {                                     │   │
│  │      void (*on_start)(GameContext*);                  │   │
│  │      void (*on_update)(GameContext*, float dt);       │   │
│  │      void (*on_stop)(GameContext*);                   │   │
│  │      void (*on_save_state)(GameContext*, Arena*);     │   │
│  │      void (*on_load_state)(GameContext*, void* blob); │   │
│  │  } game_api_t;                                        │   │
│  └───────────────────────────────────────────────────────┘   │
│                                                              │
└──────────────────────────────────────────────────────────────┘
              │ compile trigger
              ▼
┌──────────────────────────────────────────────────────────────┐
│  game.dll  (rebuilt on code change, shadow-copied to load)   │
│                                                              │
│  void uz_game_start(GameContext* ctx) { ... }                │
│  void uz_game_update(GameContext* ctx, float dt) { ... }     │
│  void uz_game_stop(GameContext* ctx) { ... }                 │
│  void uz_game_save_state(GameContext* ctx, Arena* out) { ..} │
│  void uz_game_load_state(GameContext* ctx, void* blob) { ..} │
└──────────────────────────────────────────────────────────────┘
```

---

## 3. Shader Hot Reload

Shaders are the simplest reload case — no state is lost.

### Flow

```
1. Developer edits shaders/pbr.hlsl in VSCode
2. FileWatcher detects file change (ReadDirectoryChangesW)
3. FileWatcher posts event: { path: "shaders/pbr.hlsl", type: MODIFIED }
4. ShaderCompiler recompiles: DXC → DXIL bytecode → temp file
   - On error: diagnostics sent via IPC → VSCode shows inline errors
   - On success: continue to step 5
5. On next frame boundary (render thread picks up):
   - Old PSO flushed (fence wait)
   - New PSO created from new DXIL
   - Old PSO released (ComPtr ref → 0)
6. IPC response: { "ok": true, "warnings": [] } or { "error": "..." }
7. VSCode shows ✓ or error squiggles in the shader file
```

### ShaderCompiler API

```cpp
// renderer/shader_compiler.h
struct ShaderCompileResult {
    bool         success;
    std::string  error_message;   // HLSL line:col format for VSCode
    std::vector<uint8_t> dxil;    // bytecode (empty on failure)
};

class ShaderCompiler {
public:
    static ShaderCompiler& instance();
    void init();     // loads dxcompiler.dll
    void shutdown();

    // Compile synchronously (called from FileWatcher callback thread)
    ShaderCompileResult compile_hlsl(
        const char* source_path,
        const char* entry_point,   // e.g. "VSMain" / "PSMain"
        const char* target,        // e.g. "vs_6_6" / "ps_6_6"
        Arena& scratch
    );

    // Hot-swap: waits for current frame to finish, then swaps PSO
    // Returns immediately; swap happens async on render thread
    void hot_swap_pso(const char* shader_path, std::vector<uint8_t> dxil);
};
```

### Error Format (VSCode Integration)

DXC errors are returned in a format parseable by VSCode's problem matcher:

```
shaders/pbr.hlsl(42,15): error X3013: 'normalize': no matching 1 overloads
```

The IPC handler converts this to:
```json
{
  "ok": false,
  "diagnostics": [
    { "file": "shaders/pbr.hlsl", "line": 42, "col": 15,
      "severity": "error", "message": "no matching 1 overloads for 'normalize'" }
  ]
}
```

---

## 4. Lua Script Hot Reload

Lua hot-reload is the fastest reload path — no compilation, no DLL swap. The LuaJIT VM reloads only the changed module and re-runs `on_start` on affected nodes.

### Flow

```
t=0ms    Developer saves res://game/enemy_ai.lua in VSCode
t=50ms   FileWatcher detects .lua file change
t=50ms   LuaScriptReloader queues: { path: "res://game/enemy_ai.lua" }
t=55ms   Main thread frame start: drains LuaScriptReloader queue
           Finds all nodes where CScriptRef.script_id == hash("res://game/enemy_ai.lua")
           For each affected node:
             call M:on_stop(ctx)           — cleanup (unsubscribe, release refs)
           luaL_loadfile(L, absolute_path) — parse + load new module
           lua_setglobal(L, module_key)    — replace M in VM
           For each affected node:
             call M:on_start(ctx)          — reinitialize with new code
t=60ms   Next on_update() uses new logic
```

**Total: ~60ms** — from file save to live result.

### State Preservation

Per-node `self` tables survive Lua hot-reload unchanged. Only the function definitions (`on_start`, `on_update`, `on_stop`) are replaced.

This means:
- `self.health`, `self.speed`, `self.target` — preserved
- `M.on_update` — replaced with new version
- Script re-calls `on_start` so any new fields in `on_start` are initialized

To reset `self` state explicitly: delete the `.lua` file and recreate it (triggers a fresh `on_start` with clean `self`).

### Error Handling

```
[lua hot-reload] parse error in res://game/enemy_ai.lua:14:
  unexpected symbol near 'end'
[lua hot-reload] res://game/enemy_ai.lua: reload aborted, previous version retained
```

On parse/load error:
- Old module `M` is kept in VM (previous version stays live)
- Error sent to VSCode diagnostics (file + line annotation)
- No nodes are disrupted

On `on_start` error after reload:
- Node's script is disabled for this reload cycle
- Error logged with pcall stack trace
- Fixed on next hot-reload

### Lua State Across `game.dll` Hot-Reload

When `game.dll` itself hot-reloads (because C++ changed), the entire LuaJIT VM is destroyed and recreated. To preserve Lua state, `game.dll` serializes all instance `self` tables in `on_save_state`:

```cpp
// In game.cpp
void uz_game_save_state(GameContext* ctx, Arena* out) {
    // Serialize all Lua node instances to msgpack blob
    lua_vm.serialize_all_instances(out);
}

void uz_game_load_state(GameContext* ctx, void* blob, size_t size) {
    if (!blob) return;
    // Restore self tables; NodeRef fields are re-resolved by name
    lua_vm.deserialize_all_instances(blob, size);
}
```

Only serializable values survive: `number`, `string`, `boolean`, nested `table` of those. `NodeRef` handles are re-resolved by node name after reload.

---

## 5. Scene / Material Hot Reload

When a `.uzscene` or `.uzmat` file changes on disk, the engine reloads it without stopping the render loop.

### Scene Hot Reload Flow

```
1. FileWatcher detects scenes/level1.uzscene changed
2. SceneManager::reload_file(path) called on IPC thread
3. Parse new .uzscene into a staging SceneGraph (on scratch arena)
4. Diff staging vs live scene:
   - Added nodes   → add_node() on live scene
   - Removed nodes → remove_node() on live scene
   - Modified props → set_property() on live scene
5. ECS world updated to reflect scene changes
6. Render thread picks up new world state on next frame
7. IPC response: { "ok": true, "reloaded_nodes": 5 }
```

**No state is lost**: objects in the scene not touched by the diff remain intact, including runtime state (physics velocities, script state, etc.).

### Material Hot Reload

Material files (`.uzmat`) define shader parameters for a material instance. When a `.uzmat` file changes:

```
1. FileWatcher detects materials/wood.uzmat changed
2. ResourceManager::reload_material(path) queued
3. Main thread applies queue at frame start:
   - Re-parse .uzmat file → new UzMaterial struct
   - Update asset cache entry
   - All meshes using this material see new params next frame
4. Shader recompile is NOT triggered unless the `shader` property itself changed
   - Only property values (albedo, roughness, metallic, texture refs) are reloaded
5. Affected meshes re-render with new parameters
```

**Note**: Material parameters are POD types (floats, Vec3, texture handles). Changing them never requires a DLL reload — the material instance struct is updated in-place.

---

## 5b. Audio File Hot Reload

When audio source files change (`.wav`, `.mp3`, `.ogg`), the engine re-imports them into the engine format and invalidates the resource cache.

```
1. FileWatcher detects audio/footstep.wav changed
2. AudioImporter::queue_reload(path)
3. Main thread drains audio import queue:
   - Re-import footstep.wav → footstep.uzaudio (binary)
   - Invalidate ResourceHandle for this path
4. Currently-playing voices continue on OLD buffer until they finish
5. New playbacks (after reload) use NEW buffer immediately
```

**Behavior**: Audio is resilient to mid-reload playback. Voices don't cut off; they fade out naturally on the old buffer while new sounds pick up the new version.

---

## 5c. UI Hot Reload

UI documents (`.html` / `.css`) can be reloaded without restarting the scene. Changes to layout, styling, and content are applied immediately.

```
1. FileWatcher detects ui/main_menu.html or ui/style.css changed
2. UiHtmlReloader::queue_reload(path)
3. Main thread applies reload:
   - ui_load_document(path) called again
   - New UiDocument* parsed and populated
   - Existing UiDocument* replaced
4. CSS custom property values (variables) preserved across reload
   - Re-applied after document load
5. Next frame renders with new layout/styling
```

**No script interruption**: Game logic (`game.dll`) callbacks registered on buttons/widgets remain active — they are re-wired to the new buttons after reload. No clicks are lost during the reload window.

---

## 6. Game Logic Hot Reload (DLL)

This is the most complex case. The approach is based on **shadow copy + stable C ABI**.

### Why Not Just Replace the DLL?

On Windows, `LoadLibrary` holds a file lock. You cannot overwrite a loaded DLL. Two solutions exist:

| Approach | Pros | Cons |
|----------|------|------|
| **Shadow copy** | Simple, reliable | Temp files accumulate |
| **Copy-on-write mapping** | No temp files | Complex |

UzEngine uses **shadow copy** — the simplest approach, proven by Handmade Hero, Our Machinery blog.

### Shadow Copy Flow

```
1. Developer edits src/game/player.cpp
2. FileWatcher detects .cpp change
3. HotReloadManager triggers build:
   cmake --build build --config Debug --target game -- /maxcpucount
4. Build succeeds: build/Debug/game.dll is fresh
5. HotReloadManager:
   a. Calls game_api.on_save_state() → serializes game state to blob
   b. Calls FreeLibrary(game_dll_handle)
   c. Copies game.dll → game_YYYYMMDD_HHMMSS.dll (shadow copy)
   d. LoadLibraryW(shadow copy path)
   e. GetProcAddress for all 5 functions → populate game_api_t
   f. Calls game_api.on_load_state(blob) → restores game state
6. Next update() calls new code
7. Old shadow copies cleaned up (keep last 3)
```

### State Serialization Contract

Game logic must implement save/load state to survive reload:

```cpp
// In game.dll — developer implements these
extern "C" {

void uz_game_start(GameContext* ctx) {
    // One-time init (called fresh after first load)
    ctx->player_pos = {0, 1, 0};
}

void uz_game_update(GameContext* ctx, float dt) {
    // Called every frame
    move_player(ctx, dt);
}

void uz_game_stop(GameContext* ctx) {
    // Cleanup before unload (save to disk if needed)
}

void uz_game_save_state(GameContext* ctx, Arena* out) {
    // Serialize hot-reloadable state to out arena
    // Rule: only serialize data that should survive reload
    // Rule: use simple POD structs — no pointers
    auto* s = out->make<SavedPlayerState>();
    s->position = ctx->player_pos;
    s->health   = ctx->player_health;
}

void uz_game_load_state(GameContext* ctx, void* blob, size_t blob_size) {
    // Restore state after hot reload
    if (!blob) return;  // first load — on_start was called instead
    auto* s = reinterpret_cast<SavedPlayerState*>(blob);
    ctx->player_pos    = s->position;
    ctx->player_health = s->health;
}

} // extern "C"
```

### GameContext (Stable ABI)

`GameContext` is passed from the engine to the game DLL. It exposes engine services. **The layout must never change** (add fields only at the end):

```cpp
// core/game_api.h — shared between engine and game.dll
struct GameContext {
    // Version (always first — for future compat checks)
    uint32_t api_version;      // currently 1

    // Scene access (read/write)
    Scene*   scene;

    // ECS world (game systems register here)
    entt::registry* world;

    // Input (read-only)
    const InputState* input;

    // Timing
    float    delta_time;
    double   time_elapsed;
    uint64_t frame_index;

    // Logging
    void (*log_fn)(const char* msg);

    // Resource loading
    ResourceHandle<Mesh>     (*load_mesh_fn)(const char* path);
    ResourceHandle<Material> (*load_mat_fn)(const char* path);

    // Reserved for future expansion — always at end, always zero
    uint8_t _reserved[128];
};

// Function table populated by LoadLibrary + GetProcAddress
typedef struct {
    void (*on_start)(GameContext*);
    void (*on_update)(GameContext*, float dt);
    void (*on_stop)(GameContext*);
    void (*on_save_state)(GameContext*, Arena* out);
    void (*on_load_state)(GameContext*, void* blob, size_t size);
} game_api_t;

#define UZ_GAME_API_VERSION 1
```

---

## 7. File Watcher Implementation

```cpp
// core/platform/file_watcher.h
enum class WatchEvent { Modified, Created, Deleted, Renamed };

struct FileChange {
    WatchEvent  event;
    std::string path;       // absolute path
    std::string old_path;   // only for Renamed
};

class FileWatcher {
public:
    // Watch a directory recursively
    void watch(const char* dir);

    // Poll for changes (called from IPC or dedicated thread)
    // Returns all changes since last poll
    std::vector<FileChange> poll(Arena& scratch);

    void stop();
    ~FileWatcher();

private:
    HANDLE          dir_handle_;
    OVERLAPPED      overlapped_{};
    uint8_t         buf_[65536];    // RDCW output buffer
    std::mutex      mtx_;
    std::deque<FileChange> queue_;
};
```

### Dispatch Table

The engine maps file extensions to reload handlers:

```cpp
// main.cpp or hot_reload_manager.cpp
void setup_file_watcher_dispatch(FileWatcher& watcher, ...) {
    watcher.on_change(".hlsl",   [](const FileChange& c) { shader_compiler.queue_reload(c.path); });
    watcher.on_change(".glsl",   [](const FileChange& c) { shader_compiler.queue_reload(c.path); });
    watcher.on_change(".uzscene",[](const FileChange& c) { scene_manager.reload_file(c.path); });
    watcher.on_change(".uzmat",  [](const FileChange& c) { resource_manager.reload_material(c.path); });
    // .cpp changes: trigger incremental build → reload game.dll
    watcher.on_change(".cpp",    [](const FileChange& c) { hot_reload_manager.trigger_build(); });
    watcher.on_change(".h",      [](const FileChange& c) { hot_reload_manager.trigger_build(); });
}
```

---

## 8. Why Not Godot's Approach?

Godot 4's GDExtension hot reload (`reloadable = true`) is conceptually similar but has known instability on Windows:

- **DLL lock on Windows**: Godot holds a file lock; hot reload requires a shadow copy which Godot doesn't always implement correctly (see [godot-cpp #1589](https://github.com/godotengine/godot-cpp/issues/1589))
- **CallableCustomExtension use-after-free**: function pointers stored in Callable objects survive the DLL unload, causing crashes ([forum post](https://forum.godotengine.org/t/gdextension-crash-on-editor-exit-after-hot-reload/132894))
- **Godot reloads the entire extension state**: no incremental diff, more disruption

UzEngine avoids these by:
1. **Shadow copy**: no file lock issue
2. **Explicit state contract** (`save_state`/`load_state`): no stale function pointers
3. **C ABI boundary**: vtable is a simple struct of C function pointers, not C++ virtual dispatch across DLL boundaries (which is ABI-unsafe)
4. **Engine never unloads its own state**: scene graph, renderer, ECS world stay alive during DLL swap

---

## 9. Unity Comparison

Unity's domain reload (the slow part) is C#-specific: it reloads the entire Mono/IL2CPP domain, re-initializes all static constructors, and re-runs `[InitializeOnLoad]` methods. This takes 1–10 seconds on large projects.

UzEngine avoids this because:
- **Game logic is C, not C#** — no CLR domain, no JIT warm-up
- **DLL unload is fast**: `FreeLibrary` + `LoadLibrary` takes < 50ms
- **Incremental build**: only changed `.cpp` files recompile (CMake + Ninja: typically < 3s)

Total reload time target: **< 5 seconds** from file save to new behavior running in the viewport.

---

## 10. Reload Timing Sequence (Target)

```
t=0ms    Developer saves player.cpp in VSCode
t=50ms   FileWatcher polls, detects change
t=100ms  cmake --build starts (Ninja, incremental)
t=2500ms Compile done: game.dll updated
t=2510ms HotReloadManager detects new game.dll (file write time changed)
t=2515ms game_api.on_save_state() called — state blob written to temp arena
t=2520ms FreeLibrary(old_game.dll) — memory freed, no leaked threads
t=2525ms CopyFile(game.dll → game_shadow_001.dll)
t=2530ms LoadLibraryW(game_shadow_001.dll) — DLL mapped
t=2535ms GetProcAddress × 5 — game_api_t populated
t=2540ms game_api.on_load_state(blob) — state restored
t=2545ms Next frame calls new on_update() — developer sees result
```

**Total: ~2.5 seconds** from file save to live result (depends on project size).

---

## 11. Build Integration (CMakeLists.txt)

```cmake
# game.dll — separate target, smaller rebuild surface
add_library(game SHARED
    src/game/player.cpp
    src/game/enemy.cpp
    src/game/game_logic.cpp
)
target_include_directories(game PRIVATE src/)
target_compile_definitions(game PRIVATE UZ_GAME_DLL)
target_link_libraries(game PRIVATE glm::glm)  # math only, no engine internals

# Separate output directory so FileWatcher knows where to find it
set_target_properties(game PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/game"
    OUTPUT_NAME "game"
)
```

The engine executable does **not** link against `game.dll` at link time — it uses `LoadLibraryW` at runtime only.
