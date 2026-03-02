# Plugin / Extension System Spec

## Overview

UzEngine's plugin system allows third-party developers (and the engine team itself) to extend both the editor and the runtime without modifying the core engine source. A plugin consists of:

1. A **VSCode extension** that adds editor panels, commands, and asset importers.
2. An optional **C++ DLL** that adds runtime systems, node types, and IPC handlers.
3. A **`.uzplugin` manifest** (TOML) that ties both together.

The system ships with four official optional plugins: `uz_terrain`, `uz_cinemachine`, `uz_dialogue`, and `uz_tilemap`. Third-party plugins are installed from `.uzplugin` zip archives and listed in `project.uzproj`.

---

## 1. Plugin Manifest (`.uzplugin`)

The manifest is a TOML file at the root of the plugin zip archive:

```toml
[plugin]
name                = "uz_terrain"
version             = "1.0.0"
author              = "UzEngine Team"
description         = "Height-map terrain with splatmap painting, foliage instancing, and water plane."
engine_version_min  = "0.4.0"
engine_version_max  = ""          # empty = no upper bound

[vscode]
extension_id        = "uzengine.uz-terrain"   # must match VS Marketplace / VSIX id
extension_vsix      = "uz-terrain-1.0.0.vsix" # path inside archive

[cpp]
dll                 = "uz_terrain.dll"         # optional; omit section if runtime-only TSX
entry_point         = "uz_terrain_api"         # exported symbol name (see plugin_api_t)

[permissions]
filesystem_read     = true   # allow reading project files
filesystem_write    = true   # allow writing import outputs
network             = false  # no outbound network access
spawn_process       = false  # no child process spawning
```

The `.uzplugin` archive structure:

```
uz_terrain-1.0.0.uzplugin  (ZIP rename)
├── uz_terrain.uzplugin        (manifest TOML)
├── uz_terrain.dll             (optional C++ runtime)
├── uz_terrain-1.0.0.vsix      (VSCode extension)
└── README.md
```

---

## 2. Plugin Registry in `project.uzproj`

```toml
[project]
name    = "MyGame"
version = "0.1.0"

# Enabled plugins — loaded in order
plugins = [
    "uz_terrain",
    "uz_cinemachine",
    "com.thirdparty.weather",   # third-party
]

[plugin_paths]
# Override install path for specific plugins (optional; relative to project root)
# Useful for plugins under active development
"com.thirdparty.weather" = "plugins/weather/"
```

Plugin discovery order:
1. `<project_root>/plugins/` (project-local, highest priority)
2. `%APPDATA%/UzEngine/plugins/` (user-global install)
3. `<engine_install>/plugins/` (engine-bundled)

---

## 3. Installation

Plugins are distributed as `.uzplugin` archives. Installation:

```
# Via VSCode command palette
UzEngine: Install Plugin → file picker → uz_terrain-1.0.0.uzplugin

# Via CLI
uzengine plugin install ./uz_terrain-1.0.0.uzplugin [--global | --local]
```

Installation steps:
1. Unzip archive to `%APPDATA%/UzEngine/plugins/uz_terrain/` (global) or `<project>/plugins/uz_terrain/` (local).
2. Install the VSIX into VSCode via `code --install-extension uz_terrain-1.0.0.vsix`.
3. Register plugin name in `project.uzproj` (editor prompts).
4. Reload engine + extension.

---

## 4. C++ Runtime Plugin API

### Stable C ABI

Plugin DLLs communicate with the engine via a stable C ABI struct. This prevents ABI breaks from C++ standard library or compiler version mismatches between the plugin and the engine.

```c
/* plugin_api.h — engine-provided header, part of UzEngine SDK */

/* Version the engine exports in the EngineAPI */
#define UZ_PLUGIN_API_VERSION 1

typedef struct UzPluginInfo_t {
    uint32_t    api_version;   /* must equal UZ_PLUGIN_API_VERSION */
    const char* plugin_name;
    const char* plugin_version;
} UzPluginInfo_t;

/* Function pointers the engine provides to the plugin */
typedef struct UzEngineAPI_t {
    /* Scene */
    void (*register_node_type)(const char* type_name,
                               void (*factory)(void* scene, uint32_t node_id));
    void (*unregister_node_type)(const char* type_name);

    /* IPC */
    void (*register_ipc_handler)(const char* method,
                                 void (*handler)(const char* json_in,
                                                 char* json_out, int out_size));
    void (*unregister_ipc_handler)(const char* method);

    /* Logging */
    void (*log_info)(const char* plugin_name, const char* message);
    void (*log_warn)(const char* plugin_name, const char* message);
    void (*log_error)(const char* plugin_name, const char* message);

    /* Asset system */
    void (*register_importer)(const char* extension,
                              void (*import_fn)(const char* src_path,
                                               const char* out_dir));
    void (*unregister_importer)(const char* extension);
} UzEngineAPI_t;

/* Plugin DLL must export this symbol */
typedef struct UzPluginAPI_t {
    UzPluginInfo_t info;

    /* Lifecycle */
    int  (*on_load)(const UzEngineAPI_t* engine);    /* 0 = success */
    void (*on_unload)(void);
    void (*on_update)(float dt);                     /* called each frame */
} UzPluginAPI_t;

/* Exported symbol from DLL: */
/* UzPluginAPI_t* uz_terrain_api(void);  */
```

### Plugin DLL Entry Point Example

```c
/* uz_terrain/src/plugin_main.c */
#include "plugin_api.h"

static const UzEngineAPI_t* g_engine = NULL;

static int on_load(const UzEngineAPI_t* engine) {
    g_engine = engine;
    engine->register_node_type("Terrain",    terrain_node_factory);
    engine->register_node_type("WaterPlane", water_node_factory);
    engine->register_ipc_handler("terrain.getHeightAt", ipc_get_height);
    engine->register_ipc_handler("terrain.paintLayer",  ipc_paint_layer);
    engine->register_importer(".uzterrain",  import_uzterrain);
    engine->log_info("uz_terrain", "Terrain plugin loaded");
    return 0;
}

static void on_unload(void) {
    g_engine->unregister_node_type("Terrain");
    g_engine->unregister_node_type("WaterPlane");
    g_engine->unregister_ipc_handler("terrain.getHeightAt");
    g_engine->unregister_ipc_handler("terrain.paintLayer");
    g_engine->unregister_importer(".uzterrain");
}

static UzPluginAPI_t s_api = {
    .info = {
        .api_version    = UZ_PLUGIN_API_VERSION,
        .plugin_name    = "uz_terrain",
        .plugin_version = "1.0.0",
    },
    .on_load   = on_load,
    .on_unload = on_unload,
    .on_update = NULL,  /* terrain has no per-frame update logic in plugin */
};

/* DLL export */
UzPluginAPI_t* uz_terrain_api(void) { return &s_api; }
```

### Plugin DLL Loading (Engine Side)

```cpp
// src/engine/PluginSystem.cpp
struct LoadedPlugin {
    std::string      name;
    HMODULE          dll_handle;
    UzPluginAPI_t*   api;
};

class PluginSystem {
public:
    bool load_plugin(const std::string& plugin_dir,
                     const PluginManifest& manifest,
                     const UzEngineAPI_t& engine_api);
    void unload_all();
    void update(float dt);

private:
    std::vector<LoadedPlugin> m_plugins;
};

bool PluginSystem::load_plugin(const std::string& plugin_dir,
                                const PluginManifest& manifest,
                                const UzEngineAPI_t& engine_api)
{
    if (manifest.cpp_dll.empty()) return true;  // VSCode-only plugin, no DLL

    std::string dll_path = plugin_dir + "/" + manifest.cpp_dll;
    HMODULE dll = LoadLibraryA(dll_path.c_str());
    if (!dll) {
        log_error("PluginSystem: failed to load DLL: {}", dll_path);
        return false;
    }

    using EntryFn = UzPluginAPI_t*(*)();
    auto entry = reinterpret_cast<EntryFn>(
        GetProcAddress(dll, manifest.entry_point.c_str()));
    if (!entry) {
        FreeLibrary(dll);
        log_error("PluginSystem: entry point '{}' not found", manifest.entry_point);
        return false;
    }

    UzPluginAPI_t* api = entry();
    if (!api || api->info.api_version != UZ_PLUGIN_API_VERSION) {
        FreeLibrary(dll);
        log_error("PluginSystem: API version mismatch in '{}'", manifest.name);
        return false;
    }

    int result = api->on_load(&engine_api);
    if (result != 0) {
        FreeLibrary(dll);
        log_error("PluginSystem: on_load() returned {}", result);
        return false;
    }

    m_plugins.push_back({ manifest.name, dll, api });
    log_info("PluginSystem: loaded plugin '{}'", manifest.name);
    return true;
}
```

---

## 5. Crash Isolation

Plugin DLL crashes are caught via Windows Structured Exception Handling (SEH) around the plugin entry points:

```cpp
bool safe_call_on_update(UzPluginAPI_t* api, float dt) {
    __try {
        if (api->on_update) api->on_update(dt);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        log_error("PluginSystem: plugin '{}' crashed in on_update(). Disabling.",
                  api->info.plugin_name);
        return false;
    }
}
```

A crashed plugin is disabled for the remainder of the session. The engine logs the exception code and prompts the user to report the crash.

For more complete isolation, a future Phase 5 option (`plugin_isolation = "process"` in `project.uzproj`) will move C++ plugins into a separate child process communicating via shared memory.

---

## 6. VSCode Extension Side

### Plugin VSCode Extension Manifest (`package.json`)

```json
{
  "name": "uz-terrain",
  "publisher": "uzengine",
  "activationEvents": ["workspaceContains:**/*.uzproj"],
  "extensionDependencies": ["uzengine.uzengine"],
  "contributes": {
    "commands": [
      { "command": "uzTerrain.sculpt", "title": "UzEngine: Sculpt Terrain" },
      { "command": "uzTerrain.paintLayer", "title": "UzEngine: Paint Terrain Layer" }
    ],
    "views": {
      "uzengine-sidebar": [
        { "id": "uzTerrainEditor", "name": "Terrain Editor", "type": "webview" }
      ]
    }
  }
}
```

### IPC Extension Point

Plugin VSCode extensions can send custom IPC messages using the engine's `IPCClient` API, exposed by the core extension:

```typescript
// Plugin TypeScript — calls uz_terrain IPC handler registered by the DLL
import { getUzEngineAPI } from 'uzengine';

const api = getUzEngineAPI();

// Call a plugin-registered handler (namespaced by plugin name)
const height = await api.ipc.call('terrain.getHeightAt', { x: 10, z: 5 });
await api.ipc.call('terrain.paintLayer', { layer: 'grass', position: [10, 5], radius: 3 });
```

The IPC namespace convention: `<plugin_name>.<method>`, e.g., `terrain.*`, `cinemachine.*`, `dialogue.*`.

### Registering Custom Asset Importers (VSCode Side)

A plugin VSCode extension can register drag-drop handlers for custom file extensions in the Asset Browser:

```typescript
api.assetBrowser.registerImporter({
    extensions: ['.uzterrain'],
    label: 'UzEngine Terrain',
    icon: 'mountain',
    onDrop: async (filePath) => {
        await api.ipc.call('terrain.importTerrain', { path: filePath });
    }
});
```

---

## 7. IPC Namespace Convention

Plugin handlers must be namespaced to prevent collisions:

| Pattern | Example | Owner |
|---------|---------|-------|
| `engine.*` | `engine.ping` | Core engine |
| `scene.*` | `scene.load` | Core engine |
| `debug.*` | `debug.setNavDrawEnabled` | Core engine |
| `<plugin>.*` | `terrain.getHeightAt` | Plugin (uz_terrain) |
| `<plugin>.*` | `cinemachine.setShot` | Plugin (uz_cinemachine) |

The core IPC dispatcher checks the prefix; unknown prefixes are routed to plugin handlers. If no plugin handles the method, the engine returns `{"error": "unknown_method"}`.

---

## 8. Hot-Reload

### VSCode Extension Hot-Reload

Plugin VSCode extensions reload whenever their VSIX is re-installed via the extension host restart mechanism. The core `uzengine` extension subscribes to `vscode.extensions.onDidChange` and re-queries plugin IPC capabilities.

### C++ DLL Hot-Reload

DLL hot-reload is supported in editor mode only:

1. Editor detects `.dll` file change (FileWatcher).
2. Engine calls `api->on_unload()`.
3. `FreeLibrary(dll_handle)`.
4. Build new DLL (CMake target rebuild triggered).
5. `LoadLibraryA()` on new DLL.
6. `api->on_load(&engine_api)`.
7. Scene restores its state (IPC call `scene.reload`).

This matches the pattern in `docs/specs/hot-reload.md` for game DLL hot-reload, reusing the same shadow-copy mechanism to avoid file lock issues.

---

## 9. Built-in Optional Plugins

These plugins ship with UzEngine but are disabled by default (not added to `project.uzproj` unless the user enables them):

| Plugin ID | Description | Phase |
|-----------|-------------|-------|
| `uz_terrain` | Height-map terrain, splatmap, foliage, water plane | 4 |
| `uz_cinemachine` | Cinematic camera system, dolly tracks, timeline | 5 |
| `uz_dialogue` | Dialogue graph editor, localization, voice line sync | 5 |
| `uz_tilemap` | 2D and 2.5D tile map editor, tile sets, auto-tiles | 5 |

---

## 10. Security and Permissions

Plugin permissions are declared in the manifest and enforced at load time:

| Permission | Default | Controls |
|------------|---------|---------|
| `filesystem_read` | true | Plugin can read project files |
| `filesystem_write` | true | Plugin can write `.import/` outputs |
| `network` | false | Outbound HTTP/WebSocket; requires explicit grant |
| `spawn_process` | false | ShellExecute / CreateProcess; requires explicit grant |

The permission system is advisory in Phase 4 (logged but not enforced). Phase 5 will enforce permissions via a capability-restricted sandbox for untrusted plugins.

---

## 11. IPC Handlers (Core Engine)

```
plugin.list          {}  → [{ name, version, enabled, has_dll }]
plugin.enable        { name }
plugin.disable       { name }
plugin.reload        { name }   → triggers DLL hot-reload
plugin.install       { path }   → installs .uzplugin archive
plugin.uninstall     { name }
```
