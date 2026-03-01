## 1. Plugin Manifest (`.uzplugin` TOML)

- [ ] 1.1 Define `PluginManifest` struct: `name`, `version`, `author`, `description`, `engine_version_min/max`, `vscode.extension_id`, `vscode.extension_vsix`, `cpp.dll`, `cpp.entry_point`, `permissions.*`
- [ ] 1.2 `PluginManifest::load(toml_path)` — parse TOML, validate required fields
- [ ] 1.3 Engine version compatibility check: `UZ_ENGINE_VERSION >= engine_version_min`

## 2. Stable C ABI (`plugin_api.h`)

- [ ] 2.1 Define `UzPluginInfo_t`: `api_version`, `plugin_name`, `plugin_version`
- [ ] 2.2 Define `UzEngineAPI_t` function pointer struct: `register_node_type`, `unregister_node_type`, `register_ipc_handler`, `unregister_ipc_handler`, `log_info`, `log_warn`, `log_error`, `register_importer`, `unregister_importer`
- [ ] 2.3 Define `UzPluginAPI_t`: `info`, `on_load(engine*)`, `on_unload()`, `on_update(dt)` — all C function pointers
- [ ] 2.4 `#define UZ_PLUGIN_API_VERSION 1` — increment on any ABI change
- [ ] 2.5 Ship `plugin_api.h` as part of UzEngine SDK (copied to `staged/<platform>/sdk/include/`)

## 3. PluginSystem (C++ Engine Side)

- [ ] 3.1 `PluginSystem` class: `load_plugin(plugin_dir, manifest, engine_api)`, `unload_all()`, `update(dt)`
- [ ] 3.2 `LoadLibraryA(dll_path)` + `GetProcAddress(dll, entry_point)` → `UzPluginAPI_t*`
- [ ] 3.3 API version check: mismatch → `FreeLibrary` + error log
- [ ] 3.4 `api->on_load(&engine_api)` with non-zero return → `FreeLibrary` + error log
- [ ] 3.5 `LoadedPlugin` list: `{ name, dll_handle, api }`
- [ ] 3.6 `update(dt)`: calls `safe_call_on_update(api, dt)` for each loaded plugin
- [ ] 3.7 `unload_all()`: calls `api->on_unload()` + `FreeLibrary` in reverse load order

## 4. Crash Isolation (SEH)

- [ ] 4.1 `safe_call_on_update(api, dt)` — `__try` / `__except(EXCEPTION_EXECUTE_HANDLER)` wrapper
- [ ] 4.2 On crash: log plugin name + exception code, mark plugin disabled, return false
- [ ] 4.3 Disabled plugins are skipped in subsequent `update()` calls (not unloaded — DLL may be in corrupt state)
- [ ] 4.4 Editor notification: toast message when a plugin crashes ("Plugin 'uz_terrain' crashed — disabled")

## 5. `UzEngineAPI_t` Implementation

- [ ] 5.1 `register_node_type(type_name, factory)` — adds entry to `SceneManager::node_type_registry`
- [ ] 5.2 `unregister_node_type(type_name)` — removes entry
- [ ] 5.3 `register_ipc_handler(method, handler)` — adds entry to `IPCDispatcher::plugin_handlers` map
- [ ] 5.4 `unregister_ipc_handler(method)` — removes entry
- [ ] 5.5 `log_info/warn/error` — forward to engine `Logger` with `[plugin_name]` prefix
- [ ] 5.6 `register_importer(ext, import_fn)` — adds entry to `AssetPipeline::importers` map
- [ ] 5.7 `unregister_importer(ext)` — removes entry

## 6. Plugin Discovery and Loading

- [ ] 6.1 `project.uzproj` parser: read `plugins = [...]` array
- [ ] 6.2 Plugin search order: `<project>/plugins/` → `%APPDATA%/UzEngine/plugins/` → `<engine_install>/plugins/`
- [ ] 6.3 Each plugin directory contains `<name>.uzplugin` manifest + DLL + VSIX
- [ ] 6.4 On engine start (after scene system init): load all enabled plugins in declared order
- [ ] 6.5 `plugin_paths` override table in `project.uzproj` (for plugin development)

## 7. Plugin Installation CLI

- [ ] 7.1 `uzengine plugin install <path.uzplugin>` — unzip archive, install VSIX, register in `project.uzproj`
- [ ] 7.2 `uzengine plugin uninstall <name>` — remove from `project.uzproj`, delete plugin directory
- [ ] 7.3 `uzengine plugin list` — print all installed plugins with version and enabled state

## 8. DLL Hot-Reload (Editor Mode Only)

- [ ] 8.1 FileWatcher monitors plugin DLL path for changes
- [ ] 8.2 On change: `api->on_unload()` → `FreeLibrary` → shadow-copy new DLL → `LoadLibraryA` → `api->on_load`
- [ ] 8.3 Scene state preserved across reload (IPC `scene.reload` not needed — node types re-registered)
- [ ] 8.4 If reload fails (version mismatch, on_load error): log error, keep old plugin state (not running)

## 9. IPC Handlers

- [ ] 9.1 `plugin.list {}` → `[{ name, version, enabled, has_dll }]`
- [ ] 9.2 `plugin.enable { name }` — enables plugin (adds to `project.uzproj`, loads DLL)
- [ ] 9.3 `plugin.disable { name }` — `on_unload()` + `FreeLibrary`, removes from `project.uzproj`
- [ ] 9.4 `plugin.reload { name }` — triggers hot-reload cycle
- [ ] 9.5 `plugin.install { path }` — installs `.uzplugin` archive (calls CLI install logic)
- [ ] 9.6 `plugin.uninstall { name }` — uninstalls plugin

## 10. VSCode Extension Side

- [ ] 10.1 Core extension exposes `getUzEngineAPI()` to plugin extensions via `vscode.extensions.getExtension`
- [ ] 10.2 `api.ipc.call(method, args)` — sends IPC message with plugin namespace prefix
- [ ] 10.3 `api.assetBrowser.registerImporter({ extensions, label, icon, onDrop })` — adds drag-drop handler
- [ ] 10.4 Extension loads list of enabled plugins from `project.uzproj` on workspace open
- [ ] 10.5 VSCode extension dependency: plugin extensions declare `"extensionDependencies": ["uzengine.uzengine"]` in `package.json`

## 11. Example: `uz_terrain` Plugin Skeleton

- [ ] 11.1 Create `plugins/uz_terrain/` directory with `uz_terrain.uzplugin` manifest
- [ ] 11.2 `uz_terrain.dll` C source: `on_load` registers `Terrain` + `WaterPlane` node types and `terrain.*` IPC handlers
- [ ] 11.3 `uz_terrain` VSCode extension: registers Terrain Editor panel + IPC calls
- [ ] 11.4 Verify: `uz_terrain` installs, loads, Terrain node type appears in Scene Tree "Add Node" menu

## 12. Testing

- [ ] 12.1 `uz_terrain` plugin loads without error; `plugin.list` returns it as enabled
- [ ] 12.2 Plugin registers `Terrain` node type; creating `type=Terrain` in `.uzscene` does not error
- [ ] 12.3 Plugin IPC handler `terrain.getHeightAt` is callable and returns result
- [ ] 12.4 DLL crash (injected via test): engine logs crash, continues running, plugin marked disabled
- [ ] 12.5 DLL hot-reload: modify plugin source, rebuild, hot-reload succeeds without engine restart
- [ ] 12.6 `plugin.disable` then `plugin.enable`: round-trip — node types and IPC handlers correctly removed/re-added
- [ ] 12.7 Plugin with wrong `api_version`: engine rejects it with clear error log
