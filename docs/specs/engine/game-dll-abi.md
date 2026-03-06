# Spec: game.dll C ABI Boundary

> **Status**: Phase 1 (Must exist before game.dll implementation)
> **Owner**: Interface between `render.exe` and `game.dll`
> **Purpose**: Define the stable C ABI that game.dll must export and render.exe must call. This is the only communication channel between the host process and the game module.

---

## Design Principles

1. **Pure C ABI** — No C++ types, no STL, no virtual functions across the boundary
2. **Stable** — `game_api_t` is versioned; old game.dlls can load in new engine builds
3. **Hot-reload safe** — `render.exe` can unload/reload `game.dll` without restarting
4. **No shared globals** — All state is passed through `GameContext*`; no extern globals shared across the boundary

---

## What render.exe Provides to game.dll

`render.exe` fills a `GameContext` struct and passes it to `game_dll_on_start()`. This struct contains function pointers that game.dll calls to interact with the engine.

```c
// src/engine/game_context.h
// This header is shared between render.exe and game.dll

#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

// Opaque handles used in GameContext callbacks
typedef uint64_t EntityHandle;
typedef uint64_t AudioSourceHandle;
typedef uint64_t SceneHandle;

// Forward declarations of opaque structs
typedef struct entt_registry entt_registry;   // EnTT registry (opaque in C)
typedef struct Arena Arena;

// --- Math types (POD, ABI-safe) ---
typedef struct { float x, y;       } GcVec2;
typedef struct { float x, y, z;    } GcVec3;
typedef struct { float x, y, z, w; } GcVec4;
typedef struct { float m[16];      } GcMat4;
typedef struct { float x, y, z, w; } GcQuat;

// --- GameContext: capabilities the engine provides to game.dll ---
typedef struct GameContext {
    // ABI version — game.dll checks this at startup
    uint32_t version;  // Current: 1

    // ECS registry (game.dll uses EnTT C++ API directly via this pointer)
    // NOTE: render.exe and game.dll MUST be compiled with the same EnTT version
    entt_registry* registry;

    // Frame arenas (game.dll should use these for per-frame scratch)
    Arena* frame_arena;
    Arena* level_arena;

    // --- Scene queries ---
    EntityHandle (*scene_find_by_name)(const char* name);
    EntityHandle (*scene_find_by_tag)(const char* tag, EntityHandle* out, uint32_t max_out);
    SceneHandle  (*scene_load)(const char* res_path);
    void         (*scene_unload)(SceneHandle scene);

    // --- Audio ---
    AudioSourceHandle (*audio_play_2d)(const char* clip_path, const char* bus, float volume, bool loop);
    AudioSourceHandle (*audio_play_3d)(const char* clip_path, GcVec3 pos, float volume, bool loop);
    void              (*audio_stop)(AudioSourceHandle handle);
    void              (*audio_set_bus_volume)(const char* bus, float volume);

    // --- Input (read-only query) ---
    bool  (*input_is_key_down)(uint32_t key_code);
    bool  (*input_is_action_pressed)(const char* action);
    float (*input_get_axis)(const char* axis);
    GcVec2 (*input_get_mouse_delta)(void);

    // --- IPC to VSCode (editor mode only) ---
    void  (*editor_log)(const char* message);
    void  (*editor_set_property)(const char* node_path, const char* prop, const char* value);

    // --- Time ---
    double (*time_get_elapsed_s)(void);   // Total elapsed time since engine start
    float  (*time_get_dt)(void);          // Last frame delta time

    // --- Resource loading ---
    const void* (*asset_load_raw)(const char* res_path, size_t* out_size);  // Returns pointer into level_arena
    void        (*asset_unload)(const char* res_path);

    // --- Console ---
    void (*console_log)(const char* msg);
    void (*console_warn)(const char* msg);
    void (*console_error)(const char* msg);

} GameContext;

#define GAME_CONTEXT_VERSION 1

#ifdef __cplusplus
}
#endif
```

---

## What game.dll Exports

game.dll must export these four functions with C linkage:

```c
// game_dll_api.h — exported by game.dll, called by render.exe

#ifdef __cplusplus
extern "C" {
#endif

// Called once on DLL load. ctx pointer is valid until game_dll_on_stop().
// Returns false if initialization fails (engine will log and unload).
__declspec(dllexport) bool game_dll_on_start(GameContext* ctx);

// Called every frame. dt = elapsed seconds since last frame.
__declspec(dllexport) void game_dll_on_update(float dt);

// Called every fixed physics step (60 Hz).
__declspec(dllexport) void game_dll_on_fixed_update(float fixed_dt);

// Called on DLL unload (scene change, hot-reload, quit).
// game.dll must clean up all its allocations.
__declspec(dllexport) void game_dll_on_stop(void);

#ifdef __cplusplus
}
#endif
```

---

## ABI Version Compatibility

```c
// In game_dll_on_start:
bool game_dll_on_start(GameContext* ctx) {
    if (ctx->version < GAME_CONTEXT_VERSION) {
        // Engine is older than game.dll expects — incompatible
        return false;
    }
    // ctx->version >= our version — safe to proceed
    // (new fields added to GameContext are appended at end; old fields stable)
    g_ctx = ctx;
    return true;
}
```

---

## Hot-Reload Protocol

```
1. render.exe detects game.dll change (FileWatcher)
2. Serialize current game state to level_arena scratch buffer
3. Call game_dll_on_stop() on old DLL
4. FreeLibrary(old_game_dll)
5. LoadLibrary(new_game_dll)
6. game_dll_on_start(ctx) — ctx is the same pointer, ECS registry is still valid
7. Deserialize game state from scratch buffer
8. VSCode notified: game.hot_reloaded
```

---

## File Structure

```
src/engine/
├── game_context.h      ← Shared between render.exe and game.dll (no C++)
├── game_dll_loader.h   ← render.exe: LoadLibrary, GetProcAddress, hot-reload
├── game_dll_loader.cpp ← Implementation
game.dll project:
├── game_dll_api.h      ← Exported functions declaration
├── game_main.cpp       ← game_dll_on_start/update/fixed_update/stop implementation
└── ...
```

---

## Definition of Done

- [ ] `game_context.h` — all fields above, ABI version = 1
- [ ] `game_dll_loader.h/cpp` — LoadLibrary, GetProcAddress for 4 exports, hot-reload
- [ ] `game_dll_api.h` — 4 exported functions
- [ ] Stub `game.dll` project compiles and loads/unloads without crash
- [ ] Hot-reload tested: change a string in game_dll_on_start, save, verify reload without engine restart
- [ ] ABI version mismatch returns false from game_dll_on_start and logs warning
