# Proposal: phase4-plugin-system

## Why

The engine has no extensibility mechanism. Third-party developers and studio teams cannot add new node types, editor panels, or renderer passes without modifying engine source code and rebuilding the entire project. Large optional subsystems like terrain, cinematic tools, and dialogue systems bloat the base engine binary for projects that do not need them. Without a stable C ABI plugin contract, any engine update would break all existing plugins due to C++ ABI instability across compiler versions and settings.

## What Changes

- **`.uzplugin` manifest format** — TOML file declaring plugin id, version, display name, entry-point DLL path, required engine API version, and capability flags (registers_node_types, registers_editor_panels, registers_ipc_handlers); bundled with the DLL in a zip archive for distribution
- **`UzPluginAPI_t` stable C ABI** — flat C struct of function pointers covering the full extension surface: `register_node_type`, `register_component`, `register_ipc_handler`, `register_editor_panel`, `log`, `alloc`/`free` (engine allocator), `get_scene`, `get_physics_world`; versioned with a `api_version` u32 field; binary-compatible across MSVC toolset updates
- **`PluginManager`** — loads/unloads plugin DLLs via `LoadLibraryW`/`FreeLibrary`; wraps each DLL call in Windows SEH so a crashing plugin cannot take down the engine; shadow-copies DLL to `%TEMP%\uzengine\plugins\` before loading so the source DLL can be overwritten for hot-reload
- **Hot-reload** — file watcher monitors the plugin directory; when a `.dll` modification is detected, `PluginManager` calls `plugin_shutdown()`, unloads the old DLL, shadow-copies and loads the new DLL, calls `plugin_init()`, re-registers all capabilities; max reload latency target 250 ms
- **VSCode extension plugin panel** — lists installed plugins with version, status (loaded/disabled/error), and Enable/Disable toggle; "Install Plugin" button accepts a `.uzplugin` zip drop; error state shows SEH exception code and plugin display name
- **Four official optional plugins** — `uz_terrain`, `uz_cinemachine`, `uz_dialogue`, `uz_tilemap`; distributed disabled by default in the engine install; each implements the full `UzPluginAPI_t` contract as a reference implementation

## Impact

- Affected specs: `docs/specs/plugin-system.md`, `docs/specs/editor-panels.md`, `docs/specs/scene-system.md`
- Affected code: `src/plugin/plugin_manager.cpp`, `src/plugin/plugin_manager.h`, `src/plugin/uz_plugin_api.h` (public C ABI header), `src/ipc/ipc_handlers.cpp`, `vscode-extension/src/panels/pluginPanel.ts`
- Breaking change: NO — engines without any plugins loaded are unaffected; the plugin directory is optional
- User benefit: Third-party studios can distribute engine extensions as a single `.uzplugin` zip with no engine rebuild required. Plugin crashes are caught by SEH and reported in the VSCode panel without destabilizing the editor. Official optional subsystems (terrain, cinematics) are available on demand without shipping dead code to projects that do not use them.
